/*
 * arty_hw_test -- exercise everything on the arty_hw bitstream:
 *
 *   1. my_state accumulator via axi_gpio_{control,values} (dual-channel)
 *   2. AXI Stream FIFO loopback (axi_fifo_mm_s_0, TX->RX internal loop)
 *   3. AXI DMA echo (mem -> MM2S -> S2MM -> mem, loopback in PL)
 *
 * Runs as root (needs /dev/uioN, /proc/self/pagemap, /dev/mem, mlock).
 *
 * Target: Arty Z7-20 (Zynq-7000, Cortex-A9, 32-bit ARM)
 * DMA coherency: HP0 is non-coherent -- use /dev/mem O_SYNC for uncached access.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---- Address map (matches arty_hw.tcl / arty_petalinux design) ---- */
#define ADDR_GPIO_CONTROL  0x41220000UL  /* dual-ch: ch1=control[1:0], ch2=value[31:0] */
#define ADDR_GPIO_VALUES   0x41230000UL  /* dual-ch: ch1=sum[31:0], ch2=carry[31:0] */
#define ADDR_AXI_FIFO      0x43C00000UL  /* axi_fifo_mm_s_0 (PG080) */
#define ADDR_AXI_DMA       0x40400000UL  /* axi_dma_0 */
#define MAP_SIZE           0x10000UL

/* ---- AXI GPIO (PG144) dual-channel ---- */
#define GPIO_DATA          0x00  /* Channel 1 data */
#define GPIO2_DATA         0x08  /* Channel 2 data */

/* ---- AXI Stream FIFO register offsets (PG080, AXI-Lite interface) ---- */
#define FIFO_ISR           0x00  /* Interrupt Status Register (W1C) */
#define FIFO_TDFV          0x0C  /* TX Data FIFO Vacancy (words) */
#define FIFO_TDFD          0x10  /* TX Data FIFO 32-bit write port */
#define FIFO_TLR           0x14  /* TX Length Register -- commits packet */
#define FIFO_RDFO          0x1C  /* RX Data FIFO Occupancy (words) */
#define FIFO_RDFD          0x20  /* RX Data FIFO 32-bit read port */
#define FIFO_RLR           0x24  /* RX Length Register (bit 31 = LLAST) */
#define FIFO_SRR           0x28  /* AXI4-Stream Reset (write 0xA5) */

/* ---- AXI DMA register offsets (PG021, simple/no-SG mode) ---- */
#define DMA_MM2S_DMACR     0x00
#define DMA_MM2S_DMASR     0x04
#define DMA_MM2S_SA        0x18
#define DMA_MM2S_LENGTH    0x28

#define DMA_S2MM_DMACR     0x30
#define DMA_S2MM_DMASR     0x34
#define DMA_S2MM_DA        0x48
#define DMA_S2MM_LENGTH    0x58

#define DMA_DMACR_RS       (1u << 0)
#define DMA_DMACR_RESET    (1u << 2)
#define DMA_DMASR_HALTED   (1u << 0)
#define DMA_DMASR_IDLE     (1u << 1)
#define DMA_DMASR_ERR_MASK (0x70u)

/* ---- Helpers ---- */

static int read_hex(const char *path, unsigned long *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = fscanf(f, "%lx", out);
    fclose(f);
    return (n == 1) ? 0 : -1;
}

static int find_uio_for(unsigned long addr, char *out, size_t outlen)
{
    DIR *d = opendir("/sys/class/uio");
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "uio", 3) != 0) continue;
        char p[256];
        unsigned long a = 0;
        snprintf(p, sizeof(p), "/sys/class/uio/%s/maps/map0/addr", de->d_name);
        if (read_hex(p, &a) < 0) continue;
        if (a == addr) {
            snprintf(out, outlen, "/dev/%s", de->d_name);
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

static volatile uint32_t *map_uio(unsigned long addr, const char *label, int *out_fd)
{
    char dev[64];
    if (find_uio_for(addr, dev, sizeof(dev)) < 0) {
        fprintf(stderr, "  %-20s: NO UIO at 0x%08lx\n", label, addr);
        return NULL;
    }
    int fd = open(dev, O_RDWR | O_SYNC);
    if (fd < 0) { perror(dev); return NULL; }
    void *p = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); close(fd); return NULL; }
    printf("  %-20s: %s @ 0x%08lx\n", label, dev, addr);
    *out_fd = fd;
    return (volatile uint32_t *)p;
}

static inline void w32(volatile uint32_t *base, uint32_t off, uint32_t v)
{
    base[off / 4] = v;
}

static inline uint32_t r32(volatile uint32_t *base, uint32_t off)
{
    return base[off / 4];
}

static uint32_t virt_to_phys(void *vaddr)
{
    long page_size = sysconf(_SC_PAGE_SIZE);
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) { perror("pagemap open"); return 0; }

    uint64_t entry;
    off_t off = ((uintptr_t)vaddr / (uintptr_t)page_size) * (off_t)sizeof(entry);
    if (pread(fd, &entry, sizeof(entry), off) != (ssize_t)sizeof(entry)) {
        perror("pagemap pread");
        close(fd);
        return 0;
    }
    close(fd);

    if (!(entry & (1ULL << 63))) {
        fprintf(stderr, "virt_to_phys: page not present in RAM\n");
        return 0;
    }

    uint64_t pfn = entry & ((1ULL << 55) - 1);
    if (pfn == 0) {
        fprintf(stderr, "pagemap PFN=0 -- need root or CAP_SYS_ADMIN\n");
        return 0;
    }
    return (uint32_t)(pfn * (uint64_t)page_size +
                      ((uintptr_t)vaddr & (uintptr_t)(page_size - 1)));
}

static volatile uint32_t *phys_mmap(int mem_fd, uint32_t phys, size_t len,
                                     void **base, size_t *map_size)
{
    long page_size = sysconf(_SC_PAGE_SIZE);
    off_t  aligned = (off_t)(phys & ~(uint32_t)(page_size - 1));
    size_t offset  = (size_t)(phys - (uint32_t)aligned);
    *map_size = (offset + len + (size_t)page_size - 1) & ~((size_t)page_size - 1);
    *base = mmap(NULL, *map_size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, aligned);
    if (*base == MAP_FAILED) { *base = NULL; return NULL; }
    return (volatile uint32_t *)((char *)*base + offset);
}

/* ---- Test 1: accumulator (dual-channel GPIO) ---- */

static void acc_pulse(volatile uint32_t *ctl, uint32_t op)
{
    w32(ctl, GPIO_DATA, op);
    w32(ctl, GPIO_DATA, 0);
}

static uint64_t acc_read(volatile uint32_t *val)
{
    uint32_t lo = r32(val, GPIO_DATA);
    uint32_t hi = r32(val, GPIO2_DATA);
    return ((uint64_t)hi << 32) | lo;
}

static int acc_check(const char *l, uint64_t got, uint64_t exp)
{
    int ok = got == exp;
    printf("  %-28s got=0x%016" PRIx64 " exp=0x%016" PRIx64 " %s\n",
           l, got, exp, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int test_accumulator(volatile uint32_t *ctl, volatile uint32_t *val)
{
    printf("\n===== Accumulator =====\n");
    int fails = 0;

    /* reset */
    acc_pulse(ctl, 2);
    fails += acc_check("reset", acc_read(val), 0);

    /* +5 */
    w32(ctl, GPIO2_DATA, 5);
    acc_pulse(ctl, 1);
    fails += acc_check("+5", acc_read(val), 5);

    /* +5 again */
    acc_pulse(ctl, 1);
    fails += acc_check("+5 again", acc_read(val), 10);

    /* +100 */
    w32(ctl, GPIO2_DATA, 100);
    acc_pulse(ctl, 1);
    fails += acc_check("+100", acc_read(val), 110);

    /* +0xFFFFFFFF (crosses 32-bit boundary) */
    w32(ctl, GPIO2_DATA, 0xFFFFFFFFu);
    acc_pulse(ctl, 1);
    fails += acc_check("+0xFFFFFFFF (cross)", acc_read(val), 110ull + 0xFFFFFFFFull);

    /* reset to 0 */
    acc_pulse(ctl, 2);
    fails += acc_check("reset to 0", acc_read(val), 0);

    /* +0xDEADBEEF */
    w32(ctl, GPIO2_DATA, 0xDEADBEEFu);
    acc_pulse(ctl, 1);
    fails += acc_check("+0xDEADBEEF", acc_read(val), 0xDEADBEEFull);

    /* final reset */
    acc_pulse(ctl, 2);
    w32(ctl, GPIO2_DATA, 0);
    fails += acc_check("final reset", acc_read(val), 0);

    return fails;
}

/* ---- Test 2: AXI Stream FIFO loopback (PG080) ---- */

static int fifo_wait_rx(volatile uint32_t *fifo, uint32_t timeout_ms)
{
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed++) {
        if (r32(fifo, FIFO_RDFO) != 0) return 0;
        usleep(1000);
    }
    return -1;
}

static int test_fifo(volatile uint32_t *fifo)
{
    printf("\n===== AXI Stream FIFO loopback (PG080) =====\n");
    int fails = 0;

    /* Reset the FIFO core */
    w32(fifo, FIFO_SRR, 0xA5);
    usleep(1000);
    w32(fifo, FIFO_ISR, 0xFFFFFFFF);

    uint32_t vacancy = r32(fifo, FIFO_TDFV);
    printf("  post-reset: TX vacancy=%u words\n", vacancy);

    /* Write a 4-word test packet */
    uint32_t tx_data[] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xAABBCCDD };
    uint32_t num_words = sizeof(tx_data) / sizeof(tx_data[0]);

    if (vacancy < num_words) {
        fprintf(stderr, "  TX FIFO full: vacancy=%u need=%u\n", vacancy, num_words);
        return 1;
    }

    printf("  writing %u words to TX FIFO...\n", num_words);
    for (uint32_t i = 0; i < num_words; i++) {
        w32(fifo, FIFO_TDFD, tx_data[i]);
        printf("    tx[%u] = 0x%08X\n", i, tx_data[i]);
    }

    w32(fifo, FIFO_TLR, num_words * 4);

    printf("  waiting for RX data...\n");
    if (fifo_wait_rx(fifo, 100) < 0) {
        fprintf(stderr, "  timeout waiting for RX data\n");
        return 1;
    }

    uint32_t occupancy = r32(fifo, FIFO_RDFO);
    uint32_t rlr = r32(fifo, FIFO_RLR) & 0x7FFFFFFF;
    uint32_t rx_words = rlr / 4;
    printf("  RX occupancy=%u words, RLR=%u bytes (%u words)\n",
           occupancy, rlr, rx_words);

    if (rx_words > num_words) rx_words = num_words;

    uint32_t rx_data[16] = {0};
    for (uint32_t i = 0; i < rx_words; i++)
        rx_data[i] = r32(fifo, FIFO_RDFD);

    printf("  received %u words:\n", rx_words);
    int pass = (rx_words == num_words);
    for (uint32_t i = 0; i < rx_words; i++) {
        int match = (i < num_words) && (rx_data[i] == tx_data[i]);
        printf("    rx[%u] = 0x%08X %s\n", i, rx_data[i], match ? "OK" : "MISMATCH");
        if (!match) pass = 0;
    }

    if (!pass) fails++;
    printf("  FIFO echo: %s\n", pass ? "PASS" : "FAIL");

    return fails;
}

/* ---- Test 3: AXI DMA echo ---- */

static int dma_wait_idle(volatile uint32_t *d, uint32_t sr_off, const char *name)
{
    for (int i = 0; i < 10000000; i++) {
        uint32_t sr = r32(d, sr_off);
        if (sr & DMA_DMASR_ERR_MASK) {
            fprintf(stderr, "  %s SR=0x%08x ERR\n", name, sr);
            return 1;
        }
        if (sr & DMA_DMASR_IDLE) {
            printf("  %s done after %d polls, SR=0x%08x\n", name, i, sr);
            return 0;
        }
    }
    uint32_t sr = r32(d, sr_off);
    fprintf(stderr, "  %s timeout, SR=0x%08x\n", name, sr);
    return 1;
}

static int test_dma(volatile uint32_t *dma)
{
    printf("\n===== AXI DMA echo (mem->MM2S->S2MM->mem) =====\n");
    int fails = 0;

    const uint32_t num_words = 16;
    const uint32_t len_bytes = num_words * sizeof(uint32_t);

    uint32_t *anchor_tx = NULL, *anchor_rx = NULL;
    void *tx_base = NULL, *rx_base = NULL;
    size_t tx_msize = 0, rx_msize = 0;
    int mem_fd = -1;

    if (posix_memalign((void **)&anchor_tx, 64, len_bytes) != 0 ||
        posix_memalign((void **)&anchor_rx, 64, len_bytes) != 0) {
        perror("posix_memalign");
        fails = 1; goto cleanup;
    }
    memset(anchor_tx, 0, len_bytes);
    memset(anchor_rx, 0, len_bytes);

    if (mlock(anchor_tx, len_bytes) < 0 || mlock(anchor_rx, len_bytes) < 0) {
        perror("mlock");
        fails = 1; goto cleanup;
    }

    uint32_t tx_phys = virt_to_phys(anchor_tx);
    uint32_t rx_phys = virt_to_phys(anchor_rx);
    if (!tx_phys || !rx_phys) { fails = 1; goto cleanup; }

    printf("  tx_phys=0x%08X rx_phys=0x%08X len=%u\n", tx_phys, rx_phys, len_bytes);

    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { perror("/dev/mem"); fails = 1; goto cleanup; }

    volatile uint32_t *tx_uncached = phys_mmap(mem_fd, tx_phys, len_bytes,
                                                &tx_base, &tx_msize);
    volatile uint32_t *rx_uncached = phys_mmap(mem_fd, rx_phys, len_bytes,
                                                &rx_base, &rx_msize);
    if (!tx_uncached || !rx_uncached) {
        perror("/dev/mem mmap");
        fails = 1; goto cleanup;
    }

    printf("  writing %u words via DMA loopback...\n", num_words);
    for (uint32_t i = 0; i < num_words; i++) {
        tx_uncached[i] = 0xA0000000 | i;
        printf("    tx[%u] = 0x%08X\n", i, 0xA0000000 | i);
    }

    /* Reset DMA */
    w32(dma, DMA_MM2S_DMACR, DMA_DMACR_RESET);
    w32(dma, DMA_S2MM_DMACR, DMA_DMACR_RESET);
    for (int i = 0; i < 10000; i++) {
        if (!(r32(dma, DMA_MM2S_DMACR) & DMA_DMACR_RESET) &&
            !(r32(dma, DMA_S2MM_DMACR) & DMA_DMACR_RESET)) break;
    }

    printf("  after reset:  MM2S SR=0x%08x  S2MM SR=0x%08x\n",
           r32(dma, DMA_MM2S_DMASR), r32(dma, DMA_S2MM_DMASR));

    /* Arm S2MM first */
    w32(dma, DMA_S2MM_DMACR, DMA_DMACR_RS);
    w32(dma, DMA_S2MM_DA, rx_phys);
    w32(dma, DMA_S2MM_LENGTH, len_bytes);

    /* Trigger MM2S */
    w32(dma, DMA_MM2S_DMACR, DMA_DMACR_RS);
    w32(dma, DMA_MM2S_SA, tx_phys);
    w32(dma, DMA_MM2S_LENGTH, len_bytes);

    printf("  waiting for DMA to complete...\n");
    fails += dma_wait_idle(dma, DMA_MM2S_DMASR, "MM2S");
    fails += dma_wait_idle(dma, DMA_S2MM_DMASR, "S2MM");

    if (!fails) {
        int pass = 1;
        printf("  received %u words:\n", num_words);
        for (uint32_t i = 0; i < num_words; i++) {
            uint32_t rx_val = rx_uncached[i];
            int match = (rx_val == (0xA0000000 | i));
            printf("    rx[%u] = 0x%08X %s\n", i, rx_val, match ? "OK" : "MISMATCH");
            if (!match) pass = 0;
        }
        if (!pass) fails++;
        printf("  DMA echo: %s (%u bytes round-tripped)\n",
               pass ? "PASS" : "FAIL", len_bytes);
    }

cleanup:
    if (tx_base) munmap(tx_base, tx_msize);
    if (rx_base) munmap(rx_base, rx_msize);
    if (mem_fd >= 0) close(mem_fd);
    free(anchor_tx);
    free(anchor_rx);
    return fails;
}

/* ---- main ---- */

int main(void)
{
    printf("Opening UIOs:\n");
    int fd_ctl, fd_val, fd_fifo, fd_dma;
    volatile uint32_t *ctl  = map_uio(ADDR_GPIO_CONTROL, "axi_gpio_control", &fd_ctl);
    volatile uint32_t *val  = map_uio(ADDR_GPIO_VALUES,  "axi_gpio_values",  &fd_val);
    volatile uint32_t *fifo = map_uio(ADDR_AXI_FIFO,     "axi_fifo_mm_s_0",  &fd_fifo);
    volatile uint32_t *dma  = map_uio(ADDR_AXI_DMA,      "axi_dma_0",        &fd_dma);
    if (!ctl || !val || !fifo || !dma) return 1;

    int fails = 0;
    fails += test_accumulator(ctl, val);
    fails += test_fifo(fifo);
    fails += test_dma(dma);

    printf("\n=========================================\n");
    printf("Accumulator + FIFO + DMA: %s\n", fails == 0 ? "PASS" : "FAIL");
    printf("=========================================\n");
    return fails == 0 ? 0 : 1;
}

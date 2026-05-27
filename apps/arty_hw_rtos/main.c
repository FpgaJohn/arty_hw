/*
 * arty_hw_rtos -- FreeRTOS test for the arty_hw bitstream.
 *
 * Three tests wrapped in a single FreeRTOS task:
 *   1. my_state accumulator via axi_gpio_{control,values} (dual-channel)
 *   2. AXI Stream FIFO loopback (axi_fifo_mm_s_0, PG080)
 *   3. AXI DMA echo (mem->MM2S->S2MM->mem, loopback in PL)
 *
 * Hardware (matches arty_hw.tcl / arty_petalinux design):
 *   axi_gpio_control  @ 0x4122_0000  dual-ch: ch1=control[1:0], ch2=value[31:0]
 *   axi_gpio_values   @ 0x4123_0000  dual-ch: ch1=sum[31:0], ch2=carry[31:0]
 *   axi_fifo_mm_s_0   @ 0x43C0_0000  AXI Stream FIFO (PG080), TX->RX loopback
 *   axi_dma_0          @ 0x4040_0000  AXI DMA, MM2S->S2MM loopback via HP0
 */

#include <stdint.h>
#include <string.h>

#include "xil_io.h"
#include "xil_printf.h"
#include "xil_cache.h"

#include "FreeRTOS.h"
#include "task.h"

/* ---- Address map ---- */
#define ADDR_GPIO_CONTROL  0x41220000UL
#define ADDR_GPIO_VALUES   0x41230000UL
#define ADDR_AXI_FIFO      0x43C00000UL
#define ADDR_AXI_DMA       0x40400000UL

/* ---- AXI GPIO (PG144) dual-channel ---- */
#define GPIO_DATA          0x00
#define GPIO2_DATA         0x08

/* ---- AXI Stream FIFO register offsets (PG080) ---- */
#define FIFO_ISR           0x00
#define FIFO_TDFV          0x0C
#define FIFO_TDFD          0x10
#define FIFO_TLR           0x14
#define FIFO_RDFO          0x1C
#define FIFO_RDFD          0x20
#define FIFO_RLR           0x24
#define FIFO_SRR           0x28

/* ---- AXI DMA (PG021), no-SG ---- */
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
#define DMA_DMASR_IDLE     (1u << 1)
#define DMA_DMASR_ERR_MASK (0x70u)

static inline void w32(uintptr_t base, uint32_t off, uint32_t v) { Xil_Out32(base + off, v); }
static inline uint32_t r32(uintptr_t base, uint32_t off) { return Xil_In32(base + off); }

static void print_u64(const char *prefix, uint64_t v) {
    xil_printf("%s0x%08x_%08x", prefix,
               (uint32_t)(v >> 32), (uint32_t)(v & 0xFFFFFFFFu));
}

/* DMA buffers in .bss (NOT on the task stack). */
#define DMA_BUF_SIZE 4096
static uint8_t tx_buf[DMA_BUF_SIZE] __attribute__((aligned(64)));
static uint8_t rx_buf[DMA_BUF_SIZE] __attribute__((aligned(64)));

/* ---- Test 1: GPIO accumulator ---- */

static int test_gpio(void)
{
    xil_printf("\n===== Accumulator (axi_gpio_control + values) =====\r\n");
    int fails = 0;

    #define ACC_PULSE(op) do {                                     \
        w32(ADDR_GPIO_CONTROL, GPIO_DATA, (op));                   \
        w32(ADDR_GPIO_CONTROL, GPIO_DATA, 0);                     \
    } while (0)

    #define ACC_READ()                                                          \
        ( ((uint64_t)r32(ADDR_GPIO_VALUES, GPIO2_DATA) << 32)                   \
        |  (uint64_t)r32(ADDR_GPIO_VALUES, GPIO_DATA) )

    #define ACC_CHECK(label, exp_hi, exp_lo) do {                               \
        uint64_t got = ACC_READ();                                              \
        uint64_t exp = ((uint64_t)(exp_hi) << 32) | (uint64_t)(exp_lo);         \
        int ok = (got == exp);                                                  \
        xil_printf("  %-26s  ", (label));                                       \
        print_u64("got=", got);                                                 \
        print_u64("  exp=", exp);                                               \
        xil_printf("  %s\r\n", ok ? "PASS" : "FAIL");                           \
        if (!ok) fails++;                                                       \
    } while (0)

    ACC_PULSE(2);                                                                    ACC_CHECK("reset",               0, 0);
    w32(ADDR_GPIO_CONTROL, GPIO2_DATA, 5);          ACC_PULSE(1);                    ACC_CHECK("+5",                  0, 5);
    ACC_PULSE(1);                                                                    ACC_CHECK("+5 again",            0, 10);
    w32(ADDR_GPIO_CONTROL, GPIO2_DATA, 100);        ACC_PULSE(1);                    ACC_CHECK("+100",                0, 110);
    w32(ADDR_GPIO_CONTROL, GPIO2_DATA, 0xFFFFFFFFu); ACC_PULSE(1);                   ACC_CHECK("+0xFFFFFFFF (cross)", 1, 109);
    ACC_PULSE(2);                                                                    ACC_CHECK("reset to 0",          0, 0);
    w32(ADDR_GPIO_CONTROL, GPIO2_DATA, 0xDEADBEEFu); ACC_PULSE(1);                   ACC_CHECK("+0xDEADBEEF",         0, 0xDEADBEEF);
    ACC_PULSE(2); w32(ADDR_GPIO_CONTROL, GPIO2_DATA, 0);                             ACC_CHECK("final reset",         0, 0);

    return fails;
}

/* ---- Test 2: AXI Stream FIFO loopback ---- */

static int test_fifo(void)
{
    xil_printf("\n===== AXI Stream FIFO loopback (PG080) =====\r\n");
    int fails = 0;

    w32(ADDR_AXI_FIFO, FIFO_SRR, 0xA5);
    for (volatile int d = 0; d < 10000; d++);
    w32(ADDR_AXI_FIFO, FIFO_ISR, 0xFFFFFFFF);

    uint32_t vacancy = r32(ADDR_AXI_FIFO, FIFO_TDFV);
    xil_printf("  post-reset: TX vacancy=%d words\r\n", (int)vacancy);

    uint32_t tx_data[] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xAABBCCDD };
    uint32_t num_words = sizeof(tx_data) / sizeof(tx_data[0]);

    if (vacancy < num_words) {
        xil_printf("  TX FIFO full: vacancy=%d need=%d\r\n",
                   (int)vacancy, (int)num_words);
        return 1;
    }

    xil_printf("  writing %d words to TX FIFO...\r\n", (int)num_words);
    for (uint32_t i = 0; i < num_words; i++) {
        w32(ADDR_AXI_FIFO, FIFO_TDFD, tx_data[i]);
        xil_printf("    tx[%d] = 0x%08x\r\n", (int)i, (unsigned)tx_data[i]);
    }

    w32(ADDR_AXI_FIFO, FIFO_TLR, num_words * 4);

    for (volatile int d = 0; d < 100000; d++);

    uint32_t occupancy = r32(ADDR_AXI_FIFO, FIFO_RDFO);
    uint32_t rlr = r32(ADDR_AXI_FIFO, FIFO_RLR) & 0x7FFFFFFF;
    uint32_t rx_words = rlr / 4;
    xil_printf("  RX occupancy=%d words, RLR=%d bytes (%d words)\r\n",
               (int)occupancy, (int)rlr, (int)rx_words);

    if (rx_words > num_words) rx_words = num_words;

    uint32_t rx_data[16] = {0};
    for (uint32_t i = 0; i < rx_words; i++)
        rx_data[i] = r32(ADDR_AXI_FIFO, FIFO_RDFD);

    xil_printf("  received %d words:\r\n", (int)rx_words);
    int pass = (rx_words == num_words);
    for (uint32_t i = 0; i < rx_words; i++) {
        int match = (i < num_words) && (rx_data[i] == tx_data[i]);
        xil_printf("    rx[%d] = 0x%08x %s\r\n",
                   (int)i, (unsigned)rx_data[i], match ? "OK" : "MISMATCH");
        if (!match) pass = 0;
    }

    if (!pass) fails++;
    xil_printf("  FIFO echo: %s\r\n", pass ? "PASS" : "FAIL");

    return fails;
}

/* ---- Test 3: AXI DMA echo ---- */

static int dma_wait_idle(uint32_t sr_off, const char *name)
{
    for (int i = 0; i < 10000000; i++) {
        uint32_t sr = r32(ADDR_AXI_DMA, sr_off);
        if (sr & DMA_DMASR_ERR_MASK) {
            xil_printf("  %s SR=0x%08x ERR\r\n", name, (unsigned)sr);
            return 1;
        }
        if (sr & DMA_DMASR_IDLE) {
            xil_printf("  %s done after %d polls, SR=0x%08x\r\n",
                       name, i, (unsigned)sr);
            return 0;
        }
    }
    xil_printf("  %s timeout, SR=0x%08x\r\n",
               name, (unsigned)r32(ADDR_AXI_DMA, sr_off));
    return 1;
}

static int test_dma(void)
{
    xil_printf("\n===== AXI DMA echo (mem->MM2S->S2MM->mem) =====\r\n");
    int fails = 0;
    const uint32_t buf_sz = DMA_BUF_SIZE;

    uint32_t tx_pa = (uint32_t)(uintptr_t)tx_buf;
    uint32_t rx_pa = (uint32_t)(uintptr_t)rx_buf;

    uint32_t *tx32 = (uint32_t *)tx_buf;
    uint32_t *rx32 = (uint32_t *)rx_buf;
    for (uint32_t i = 0; i < buf_sz / 4; i++)
        tx32[i] = 0xA0000000 | i;
    memset(rx_buf, 0, buf_sz);

    xil_printf("  tx PA=0x%08x  rx PA=0x%08x  size=%d B\r\n",
               (unsigned)tx_pa, (unsigned)rx_pa, (int)buf_sz);

    Xil_DCacheFlushRange((UINTPTR)tx_buf, buf_sz);
    Xil_DCacheInvalidateRange((UINTPTR)rx_buf, buf_sz);

    w32(ADDR_AXI_DMA, DMA_MM2S_DMACR, DMA_DMACR_RESET);
    w32(ADDR_AXI_DMA, DMA_S2MM_DMACR, DMA_DMACR_RESET);
    int sp = 0;
    while ((r32(ADDR_AXI_DMA, DMA_MM2S_DMACR) & DMA_DMACR_RESET) && sp < 10000) sp++;
    while ((r32(ADDR_AXI_DMA, DMA_S2MM_DMACR) & DMA_DMACR_RESET) && sp < 10000) sp++;

    w32(ADDR_AXI_DMA, DMA_S2MM_DMACR, DMA_DMACR_RS);
    w32(ADDR_AXI_DMA, DMA_S2MM_DA, rx_pa);
    w32(ADDR_AXI_DMA, DMA_S2MM_LENGTH, buf_sz);

    w32(ADDR_AXI_DMA, DMA_MM2S_DMACR, DMA_DMACR_RS);
    w32(ADDR_AXI_DMA, DMA_MM2S_SA, tx_pa);
    w32(ADDR_AXI_DMA, DMA_MM2S_LENGTH, buf_sz);

    fails += dma_wait_idle(DMA_MM2S_DMASR, "MM2S");
    fails += dma_wait_idle(DMA_S2MM_DMASR, "S2MM");

    Xil_DCacheInvalidateRange((UINTPTR)rx_buf, buf_sz);

    if (!fails) {
        if (memcmp(tx_buf, rx_buf, buf_sz) != 0) {
            int diffs = 0;
            for (uint32_t i = 0; i < buf_sz / 4 && diffs < 4; i++) {
                if (tx32[i] != rx32[i]) {
                    xil_printf("    [%4d] tx=0x%08x  rx=0x%08x\r\n",
                               (int)i, (unsigned)tx32[i], (unsigned)rx32[i]);
                    diffs++;
                }
            }
            fails++;
            xil_printf("  DMA echo: FAIL (data mismatch)\r\n");
        } else {
            xil_printf("  DMA echo: PASS (%d bytes round-tripped)\r\n", (int)buf_sz);
        }
    }

    return fails;
}

/* ---- FreeRTOS task ---- */

static void test_task(void *param)
{
    (void)param;

    xil_printf("\r\n=========================================\r\n");
    xil_printf("arty_hw_rtos FreeRTOS test\r\n");
    xil_printf("=========================================\r\n");

    int fails = 0;
    fails += test_gpio();
    fails += test_fifo();
    fails += test_dma();

    xil_printf("\r\n=========================================\r\n");
    xil_printf("RESULT: %s -- %d failures\r\n",
               fails == 0 ? "ALL PASS" : "FAIL", fails);
    xil_printf("=========================================\r\n");

    vTaskDelete(NULL);
}

int main(void)
{
    xTaskCreate(test_task, "test", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    return 0;
}

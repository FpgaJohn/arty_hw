#!/bin/bash
# list_uio.sh -- enumerate UIO devices on the Arty Z7-20 board.
# Run on the target: bash list_uio.sh

echo "=== UIO devices ==="
for d in /sys/class/uio/uio*; do
    [ -d "$d" ] || continue
    name=$(cat "$d/name" 2>/dev/null)
    addr=$(cat "$d/maps/map0/addr" 2>/dev/null)
    size=$(cat "$d/maps/map0/size" 2>/dev/null)
    echo "  $(basename "$d"): $name @ $addr (size $size)"
done

echo ""
echo "=== CPU temperature ==="
if [ -d /sys/class/hwmon ]; then
    for hw in /sys/class/hwmon/hwmon*; do
        temp=$(cat "$hw/temp1_input" 2>/dev/null)
        if [ -n "$temp" ]; then
            echo "  $(( temp / 1000 )).$(( (temp % 1000) / 100 )) C"
        fi
    done
fi

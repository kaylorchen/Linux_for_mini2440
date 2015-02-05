#!/bin/sh
make clean
make zImage
cp /opt/Kernel_Study/linux-2.6.32.2/arch/arm/boot/zImage /opt/zImage
mkimage -n 'mini2440_linux' -A arm -O linux -T kernel -C none -a 0x30008000 -e 0x30008040 -d /opt/Kernel_Study/linux-2.6.32.2/arch/arm/boot/zImage /opt/uImage

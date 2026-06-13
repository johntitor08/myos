#!/bin/bash
cd /mnt/c/main/myos || exit 1
MAC=52:54:00:12:34:56
sudo pkill -f qemu-system-i386 2>/dev/null
sleep 1
sudo ip link del tap0 2>/dev/null
sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.2.2/24 dev tap0
sudo ip link set tap0 up
sudo ip neigh flush dev tap0 2>/dev/null        # NO static ARP: test guest ARP reply
sudo sysctl -qw net.ipv4.conf.all.rp_filter=0 2>/dev/null
sudo sysctl -qw net.ipv4.conf.tap0.rp_filter=0 2>/dev/null
sudo rm -f /tmp/qmon

sudo qemu-system-i386 -fda myos.img -boot a \
  -netdev tap,id=n0,ifname=tap0,script=no,downscript=no \
  -device rtl8139,netdev=n0,mac="$MAC" \
  -display none -no-reboot -monitor unix:/tmp/qmon,server,nowait > /tmp/qemu.log 2>&1 &
echo "booting (8s)..."
sleep 8

echo "=== TEST 1: host -> guest ping (no static ARP; needs guest ARP reply) ==="
ping -c 3 -W 2 10.0.2.15; echo "rc=$?"
echo "host ARP table for tap0:"; ip neigh show dev tap0

echo "=== TEST 2: guest ping 10.0.2.2 (guest -> host) ==="
sudo python3 /mnt/c/main/myos/mon.py type "ping 10.0.2.2" 2>&1
sleep 7
echo "--- guest screen ---"
sudo python3 /mnt/c/main/myos/mon.py screen 2>&1 | grep -iE "yanit|zaman|ping"

sudo pkill -f qemu-system-i386 2>/dev/null
sudo ip link del tap0 2>/dev/null
echo "=== DONE ==="

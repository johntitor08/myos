#!/bin/bash
cd /mnt/c/main/myos || exit 1
MAC=52:54:00:12:34:56
sudo pkill -f qemu-system-i386 2>/dev/null; sleep 1
sudo ip link del tap0 2>/dev/null
sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.2.2/24 dev tap0
sudo ip link set tap0 up
sudo sysctl -qw net.ipv4.conf.all.rp_filter=0 2>/dev/null
sudo sysctl -qw net.ipv4.icmp_echo_ignore_broadcasts=0 2>/dev/null
sudo rm -f /tmp/qmon /tmp/t2.pcap
sudo qemu-system-i386 -fda myos.img -boot a \
  -netdev tap,id=n0,ifname=tap0,script=no,downscript=no \
  -device rtl8139,netdev=n0,mac=$MAC \
  -object filter-dump,id=d0,netdev=n0,file=/tmp/t2.pcap \
  -display none -no-reboot -monitor unix:/tmp/qmon,server,nowait >/tmp/qemu.log 2>&1 &
echo "booting..."
sleep 8
echo "sending ping 10.0.2.2 in guest..."
sudo python3 mon.py type "ping 10.0.2.2" >/dev/null 2>&1
sleep 6
sudo pkill -f qemu-system-i386 2>/dev/null
sudo ip link del tap0 2>/dev/null
echo "=== ICMP/ARP frames ==="
python3 - << "PY"
import struct
d=open("/tmp/t2.pcap","rb").read();off=24;n=0
G="525400123456"
while off+16<=len(d):
    _,_,incl,_=struct.unpack("<IIII",d[off:off+16]);off+=16
    f=d[off:off+incl];off+=incl;n+=1
    if len(f)<34: continue
    et=(f[12]<<8)|f[13]
    src="".join("%02x"%b for b in f[6:12])
    who="GUEST" if src==G else "host "
    if et==0x0806:
        op=(f[20]<<8)|f[21]; print("%s ARP op=%d"%(who,op))
    elif et==0x0800 and f[23]==1:
        sip=".".join(str(x) for x in f[26:30]); dip=".".join(str(x) for x in f[30:34])
        print("%s ICMP type=%d %s->%s"%(who,f[34],sip,dip))
print("total frames",n)
PY
echo "=== DONE ==="

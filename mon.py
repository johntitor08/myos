import socket, time, re, sys

mode = sys.argv[1] if len(sys.argv) > 1 else "screen"
s = socket.socket(socket.AF_UNIX)
s.connect("/tmp/qmon")
time.sleep(0.3)

def cmd(c, wait=0.4):
    s.sendall((c + "\n").encode())
    time.sleep(wait)
    buf = b""
    s.settimeout(0.4)
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    except Exception:
        pass
    return buf.decode(errors="replace")

cmd("")

if mode == "type":
    text = sys.argv[2] if len(sys.argv) > 2 else "netinfo"
    km = {" ": "spc", ".": "dot"}
    for ch in text:
        cmd("sendkey " + km.get(ch, ch), 0.06)
    cmd("sendkey ret", 0.6)
    sys.exit(0)

out = ""
for _ in range(8):
    out += cmd("xp /4000xb 0xb8000", 0.6)
    if out.count("0x") > 3800:
        break
vals = []
for line in out.splitlines():
    if ":" not in line:
        continue
    rhs = line.split(":", 1)[1]
    vals += [int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", rhs)]
def score(start):
    return sum(1 for c in vals[start::2] if 32 <= c < 127)
best = 0 if score(0) >= score(1) else 1
chars = vals[best::2]
for r in range(25):
    row = chars[r*80:(r+1)*80]
    if not row:
        break
    print("|" + "".join(chr(c) if 32 <= c < 127 else " " for c in row).rstrip() + "|")

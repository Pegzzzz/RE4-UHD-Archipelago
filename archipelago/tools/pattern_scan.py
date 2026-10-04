"""Usage: python pattern_scan.py <bio4.exe> <dllmain folder>
(reports hook patterns found 0 times, or a different number of times than the code expects)"""
"""Scan every hook::pattern used by re4_tweaks against an exe, report match counts vs. what the code expects."""
import re, sys, glob, struct

exe = open(sys.argv[1], 'rb').read()
src_dir = sys.argv[2]

# PE sections: scan .text and everything (patterns are usually in code)
pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]
optsz = struct.unpack_from('<H', exe, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = exe[o:o + 8].rstrip(b'\0').decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', exe, o + 8)
    secs.append((name, raddr, rsize))
blob = b''.join(exe[r:r + s] for _, r, s in secs)

def to_regex(p):
    out = b''
    for tok in p.split():
        out += b'.' if tok in ('?', '??') else re.escape(bytes([int(tok, 16)]))
    return re.compile(out, re.S)

results = []
for f in sorted(glob.glob(src_dir + '/*.cpp')):
    text = open(f, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'hook::pattern\(\s*"([0-9A-Fa-f\? ]+)"\s*\)(\s*\.count\((\d+)\))?', text):
        pat = m.group(1).strip()
        line = text.count('\n', 0, m.start()) + 1
        # expected count: .count(N) right after, or on the next use of `pattern` in the following lines
        exp = m.group(3)
        if exp is None:
            nxt = re.search(r'pattern\.count\((\d+)\)', text[m.end():m.end() + 400])
            exp = nxt.group(1) if nxt else None
        n = len(to_regex(pat).findall(blob)) if pat else -1
        results.append((f.split('/')[-1], line, pat, exp, n))

bad = [r for r in results if r[4] == 0 or (r[3] is not None and int(r[3]) != r[4])]
print(f"{len(results)} patterns, {len(bad)} mismatched")
for r in bad:
    print(f"  {r[0]}:{r[1]} expected={r[3]} found={r[4]}  {r[2][:70]}")

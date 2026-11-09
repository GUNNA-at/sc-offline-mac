# List (and optionally extract) entries of Star Citizen's Data.p4k whose name matches a regex, streaming the central
# directory so memory stays small. usage: p4kfind.py <Data.p4k> <regex> [outdir]
import sys, struct, re, os, subprocess
path, rx = sys.argv[1], re.compile(sys.argv[2], re.I); outdir = sys.argv[3] if len(sys.argv) > 3 else None
f = open(path, 'rb'); f.seek(0, 2); size = f.tell()
tail_len = min(size, 1 << 20); f.seek(size - tail_len); tail = f.read(tail_len)
i = tail.rfind(b'PK\x06\x07')
if i < 0: sys.exit('no ZIP64 locator')
eocd64_off = struct.unpack_from('<Q', tail, i + 8)[0]
f.seek(eocd64_off); rec = f.read(56)
if rec[:4] != b'PK\x06\x06': sys.exit('no ZIP64 end record')
total, cd_size, cd_off = struct.unpack_from('<QQQ', rec, 32)
print('entries %d, central directory %.1f MB at offset %d' % (total, cd_size / 2**20, cd_off), file=sys.stderr)
f.seek(cd_off); buf = b''; pos = 0; left = cd_size; hits = []; n = 0
def need(k):
    global buf, pos, left
    while len(buf) - pos < k and left > 0:
        chunk = f.read(min(left, 8 << 20)); left -= len(chunk); buf = buf[pos:] + chunk; pos = 0
    return len(buf) - pos >= k
while need(46):
    if buf[pos:pos + 4] != b'PK\x01\x02': break
    (ver, need_ver, flags, method, mtime, mdate, crc, csize, usize, nlen, elen, clen, disk, iattr, eattr, lho) = struct.unpack_from('<HHHHHHIIIHHHHHII', buf, pos + 4)
    if not need(46 + nlen + elen + clen): break
    name = buf[pos + 46:pos + 46 + nlen].decode('utf-8', 'replace'); extra = buf[pos + 46 + nlen:pos + 46 + nlen + elen]
    pos += 46 + nlen + elen + clen; n += 1
    if rx.search(name):
        e = 0
        while e + 4 <= len(extra):
            hid, hsz = struct.unpack_from('<HH', extra, e)
            if hid == 1:
                q = e + 4
                if usize == 0xFFFFFFFF: usize = struct.unpack_from('<Q', extra, q)[0]; q += 8
                if csize == 0xFFFFFFFF: csize = struct.unpack_from('<Q', extra, q)[0]; q += 8
                if lho == 0xFFFFFFFF: lho = struct.unpack_from('<Q', extra, q)[0]; q += 8
            e += 4 + hsz
        hits.append((name, method, flags, csize, usize, lho, extra))
print('scanned %d entries, %d match' % (n, len(hits)), file=sys.stderr)
for name, method, flags, csize, usize, lho, extra in hits:
    print('%s | method %d flags 0x%x | %d -> %d bytes | extra %d bytes' % (name, method, flags, csize, usize, len(extra)))
    if not outdir: continue
    f.seek(lho); lh = f.read(30)
    nlen, elen = struct.unpack_from('<HH', lh, 26)
    f.seek(lho + 30 + nlen + elen); data = f.read(csize)
    out = os.path.join(outdir, name.replace('\\', '/').replace('/', '__')); os.makedirs(outdir, exist_ok=True)
    raw = data
    def unz(d):
        if d[:4] == b'\x28\xb5\x2f\xfd':
            import compression.zstd as z; return z.decompress(d)
        return None
    res = raw if method == 0 else unz(raw)
    if res is None:                                   # not plain zstd: the archive's AES layer (zero IV), then zstd
        key = '5E7A2002302EEB1A3BB617C30FDE1E47'
        pad = raw + b'\0' * ((16 - len(raw) % 16) % 16)
        dec = subprocess.run(['openssl', 'enc', '-d', '-aes-128-cbc', '-K', key, '-iv', '0' * 32, '-nopad'], input=pad, capture_output=True).stdout
        res = unz(dec.rstrip(b'\0')) or unz(dec) or (dec if method == 0 else None)
    if res is None: print('   could not decode (first bytes %s)' % raw[:8].hex()); continue
    open(out, 'wb').write(res); print('   -> %s (%d bytes)' % (out, len(res)))

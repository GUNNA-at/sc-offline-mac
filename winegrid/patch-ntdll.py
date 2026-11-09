#!/usr/bin/env python3
"""Add (or check for) a weak dependency on a dylib in a thin 64-bit Mach-O file, in place.
usage: patch-ntdll.py check <macho> <dylib-path>     exit 0 if present
       patch-ntdll.py add   <macho> <dylib-path>     appends an LC_LOAD_WEAK_DYLIB in the header padding
The file must be re-signed afterwards (codesign -s - --force)."""
import struct, sys

LC_SEGMENT_64, LC_LOAD_WEAK_DYLIB, LC_LOAD_DYLIB = 0x19, 0x80000018, 0xC

def parse(data):
    magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack_from('<IiiIIIII', data, 0)
    if magic != 0xFEEDFACF: sys.exit('not a thin 64-bit Mach-O file')
    off, cmds, first_data = 32, [], len(data)
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, off)
        name = None
        if cmd in (LC_LOAD_WEAK_DYLIB, LC_LOAD_DYLIB):
            noff = struct.unpack_from('<I', data, off + 8)[0]
            name = data[off + noff:off + size].split(b'\0')[0].decode()
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from('<I', data, off + 64)[0]
            for i in range(nsects):
                soff = struct.unpack_from('<I', data, off + 72 + i * 80 + 48)[0]
                if soff: first_data = min(first_data, soff)
        cmds.append((cmd, off, size, name)); off += size
    return ncmds, sizeofcmds, cmds, first_data

def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ('check', 'add'): sys.exit(__doc__)
    mode, path, dylib = sys.argv[1:]
    data = bytearray(open(path, 'rb').read())
    ncmds, sizeofcmds, cmds, first_data = parse(data)
    present = any(name == dylib for _, _, _, name in cmds)
    if mode == 'check': sys.exit(0 if present else 1)
    if present: print('already present'); return
    name = dylib.encode() + b'\0'
    size = (24 + len(name) + 7) & ~7
    end = 32 + sizeofcmds
    if end + size > first_data: sys.exit('no room in the header: %d bytes free, %d needed' % (first_data - end, size))
    if any(data[end:end + size]): sys.exit('header padding is not empty')
    cmd = struct.pack('<IIIIII', LC_LOAD_WEAK_DYLIB, size, 24, 2, 0x10000, 0x10000) + name
    data[end:end + size] = cmd + b'\0' * (size - len(cmd))
    struct.pack_into('<II', data, 16, ncmds + 1, sizeofcmds + size)
    open(path, 'wb').write(data)
    print('added weak dependency on %s (%d bytes of %d free header bytes used)' % (dylib, size, first_data - end))

main()

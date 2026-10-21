"""E3: add a custom metadata stream '#MSIL' to an IL-only PE32+ assembly.

The metadata root is copied into a new section '.msilmd' at the end of the
image with one more stream header; the CLI header's MetaData directory is
pointed at it. Usage: addstream.py <in.dll> <payload> <out.dll>
"""
import struct, sys

src, payload_path, dst = sys.argv[1:4]
b = bytearray(open(src, "rb").read())
payload = open(payload_path, "rb").read()

pe = struct.unpack_from("<I", b, 0x3C)[0]
assert b[pe:pe + 4] == b"PE\0\0"
coff = pe + 4
nsec = struct.unpack_from("<H", b, coff + 2)[0]
opt_size = struct.unpack_from("<H", b, coff + 16)[0]
opt = coff + 20
assert struct.unpack_from("<H", b, opt)[0] == 0x20B, "PE32+ only"
sect_align, file_align = struct.unpack_from("<II", b, opt + 32)
size_of_headers = struct.unpack_from("<I", b, opt + 60)[0]
ddir = opt + 112
cli_rva, cli_size = struct.unpack_from("<II", b, ddir + 14 * 8)
sec = opt + opt_size


def sections():
    for i in range(nsec):
        o = sec + 40 * i
        name = b[o:o + 8].rstrip(b"\0").decode()
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", b, o + 8)
        yield o, name, vsize, va, rawsize, rawptr


def rva2off(rva):
    for o, name, vsize, va, rawsize, rawptr in sections():
        if va <= rva < va + max(vsize, rawsize):
            return rawptr + rva - va
    raise ValueError(hex(rva))


cli = rva2off(cli_rva)
md_rva, md_size = struct.unpack_from("<II", b, cli + 8)
md = bytes(b[rva2off(md_rva):rva2off(md_rva) + md_size])
assert md[:4] == b"BSJB"
ver_len = struct.unpack_from("<I", md, 12)[0]
p = 16 + ver_len
flags, nstreams = struct.unpack_from("<HH", md, p)
p += 4
streams = []
for _ in range(nstreams):
    off, size = struct.unpack_from("<II", md, p)
    p += 8
    e = md.index(b"\0", p)
    name = md[p:e]
    p = (e + 4) & ~3
    streams.append((name, off, size))
hdr_end = p

# New root: same header, one more stream header, then the old stream data, then the payload.
new_hdrs = b""
names = [s[0] for s in streams] + [b"#MSIL"]
hdr_len = 16 + ver_len + 4 + sum(8 + ((len(n) + 4) & ~3) for n in names)
shift = hdr_len - hdr_end
data = bytearray(md[hdr_end:])
pay_off = hdr_len + len(data)
pay_off = (pay_off + 3) & ~3
for name, off, size in streams:
    new_hdrs += struct.pack("<II", off + shift, size) + name + b"\0" * (((len(name) + 4) & ~3) - len(name))
new_hdrs += struct.pack("<II", pay_off, (len(payload) + 3) & ~3) + b"#MSIL\0\0\0"
root = bytearray(md[:16 + ver_len]) + struct.pack("<HH", flags, nstreams + 1) + new_hdrs
assert len(root) == hdr_len
root += data
root += b"\0" * (pay_off - len(root))
root += payload + b"\0" * (((len(payload) + 3) & ~3) - len(payload))

# Append the section.
last = list(sections())[-1]
new_va = (last[3] + last[2] + sect_align - 1) // sect_align * sect_align
new_raw = (len(b) + file_align - 1) // file_align * file_align
raw_size = (len(root) + file_align - 1) // file_align * file_align
hdr_off = sec + 40 * nsec
assert hdr_off + 40 <= size_of_headers, "no room for a section header"
b[hdr_off:hdr_off + 40] = struct.pack("<8sIIIIIIHHI", b".msilmd", len(root), new_va, raw_size, new_raw, 0, 0, 0, 0, 0x40000040)
struct.pack_into("<H", b, coff + 2, nsec + 1)
struct.pack_into("<I", b, opt + 56, (new_va + len(root) + sect_align - 1) // sect_align * sect_align)  # SizeOfImage
b += b"\0" * (new_raw - len(b))
b += root + b"\0" * (raw_size - len(root))
struct.pack_into("<II", b, cli + 8, new_va, len(root))
open(dst, "wb").write(b)
print(f"metadata {md_size} -> {len(root)} bytes, '#MSIL' stream of {len(payload)} bytes at root offset {pay_off}")

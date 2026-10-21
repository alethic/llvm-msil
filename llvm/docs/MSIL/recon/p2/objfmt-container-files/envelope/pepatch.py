"""pepatch.py - add link information to an ilasm-built PE (assembly or netmodule).

  python pepatch.py stream  in.dll out.dll [name]   extra metadata stream (default '#MSILLink')
  python pepatch.py section in.dll out.dll [name]   extra PE section (default '.msillnk')
  python pepatch.py info    in.dll                  print sections, CLI header and streams

The payload is a small binary symbol table (magic 'MSILLINK', version, then
NUL-terminated symbol names), standing in for a CIL object's link data.
Both modes rebuild nothing else: the metadata (stream mode) is copied into a
new section and the CLI header's MetaData directory is repointed to it.
"""
import struct, sys

PAYLOAD = b"MSILLINK" + struct.pack("<II", 1, 3) + b"twice\0helper\0counter\0"


def align(x, a):
    return (x + a - 1) & ~(a - 1)


class PE:
    def __init__(self, data):
        self.d = bytearray(data)
        d = self.d
        self.pe = struct.unpack_from("<I", d, 0x3C)[0]
        assert d[self.pe:self.pe + 4] == b"PE\0\0"
        self.coff = self.pe + 4
        (self.machine, self.nsec, _, _, _, self.optsize, _) = struct.unpack_from("<HHIIIHH", d, self.coff)
        self.opt = self.coff + 20
        self.magic = struct.unpack_from("<H", d, self.opt)[0]
        self.plus = self.magic == 0x20B
        self.secalign, self.filealign = struct.unpack_from("<II", d, self.opt + 32)
        self.dd = self.opt + (112 if self.plus else 96)
        self.sectab = self.opt + self.optsize

    def sections(self):
        out = []
        for i in range(self.nsec):
            o = self.sectab + 40 * i
            name = self.d[o:o + 8].rstrip(b"\0").decode()
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", self.d, o + 8)
            chars = struct.unpack_from("<I", self.d, o + 36)[0]
            out.append((name, vsize, va, rawsize, rawptr, chars))
        return out

    def rva2off(self, rva):
        for (_, vsize, va, rawsize, rawptr, _) in self.sections():
            if va <= rva < va + max(vsize, rawsize):
                return rva - va + rawptr
        raise ValueError("rva 0x%x" % rva)

    def cli(self):
        rva, size = struct.unpack_from("<II", self.d, self.dd + 14 * 8)
        return self.rva2off(rva)

    def add_section(self, name, payload, chars=0x40000040):
        secs = self.sections()
        hdr_end = self.sectab + 40 * (self.nsec + 1)
        size_of_headers = struct.unpack_from("<I", self.d, self.opt + 60)[0]
        first_raw = min(s[4] for s in secs if s[4])
        assert hdr_end <= size_of_headers and hdr_end <= first_raw, "no room for a section header"
        last = max(secs, key=lambda s: s[2])
        va = align(last[2] + max(last[1], 1), self.secalign)
        rawptr = align(len(self.d), self.filealign)
        rawsize = align(len(payload), self.filealign)
        self.d.extend(b"\0" * (rawptr - len(self.d)))
        self.d.extend(payload + b"\0" * (rawsize - len(payload)))
        o = self.sectab + 40 * self.nsec
        struct.pack_into("<8sIIIIIIHHI", self.d, o, name.encode()[:8], len(payload), va, rawsize, rawptr, 0, 0, 0, 0, chars)
        self.nsec += 1
        struct.pack_into("<H", self.d, self.coff + 2, self.nsec)
        struct.pack_into("<I", self.d, self.opt + 56, align(va + len(payload), self.secalign))  # SizeOfImage
        return va


def parse_root(md):
    sig, maj, minr, res, vlen = struct.unpack_from("<IHHII", md, 0)
    assert sig == 0x424A5342
    ver = md[16:16 + vlen]
    p = 16 + vlen
    flags, n = struct.unpack_from("<HH", md, p)
    p += 4
    streams = []
    for _ in range(n):
        off, size = struct.unpack_from("<II", md, p)
        q = md.index(b"\0", p + 8)
        name = md[p + 8:q].decode()
        p = align(q + 1, 4)
        streams.append((name, md[off:off + size]))
    return maj, minr, ver, flags, streams


def build_root(maj, minr, ver, flags, streams):
    hdr = struct.pack("<IHHII", 0x424A5342, maj, minr, 0, len(ver)) + ver + struct.pack("<HH", flags, len(streams))
    names = [s[0].encode() + b"\0" for s in streams]
    names = [n + b"\0" * (align(len(n), 4) - len(n)) for n in names]
    hsize = len(hdr) + sum(8 + len(n) for n in names)
    off = align(hsize, 4)
    table, body = b"", b""
    for (name, data), nb in zip(streams, names):
        data = data + b"\0" * (align(len(data), 4) - len(data))
        table += struct.pack("<II", off + len(body), len(data)) + nb
        body += data
    out = hdr + table
    out += b"\0" * (off - len(out))
    return out + body


def main():
    mode, src = sys.argv[1], sys.argv[2]
    pe = PE(open(src, "rb").read())
    if mode == "info":
        for s in pe.sections():
            print("section %-8s vsize=0x%x va=0x%x raw=0x%x ptr=0x%x chars=0x%08x" % s)
        c = pe.cli()
        mdrva, mdsize = struct.unpack_from("<II", pe.d, c + 8)
        mdoff = pe.rva2off(mdrva)
        maj, minr, ver, flags, streams = parse_root(bytes(pe.d[mdoff:mdoff + mdsize]))
        print("metadata %s at rva 0x%x size 0x%x: %s" % (ver.rstrip(b"\0").decode(), mdrva, mdsize,
              ", ".join("%s(0x%x)" % (n, len(b)) for n, b in streams)))
        return
    dst = sys.argv[3]
    if mode == "section":
        name = sys.argv[4] if len(sys.argv) > 4 else ".msillnk"
        pe.add_section(name, PAYLOAD)
    elif mode == "stream":
        name = sys.argv[4] if len(sys.argv) > 4 else "#MSILLink"
        c = pe.cli()
        mdrva, mdsize = struct.unpack_from("<II", pe.d, c + 8)
        mdoff = pe.rva2off(mdrva)
        maj, minr, ver, flags, streams = parse_root(bytes(pe.d[mdoff:mdoff + mdsize]))
        streams.append((name, PAYLOAD))
        newmd = build_root(maj, minr, ver, flags, streams)
        va = pe.add_section(".msilmd", newmd)
        struct.pack_into("<II", pe.d, c + 8, va, len(newmd))
    open(dst, "wb").write(pe.d)


if __name__ == "__main__":
    main()

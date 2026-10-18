"""The DDS textures in SvR 2008's archives, and the content key the port finds them by:
FNV-1a 64 over the top level's blocks (what src/native/svr_pad_art.cpp computes on load)."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svrfmt  # noqa: E402


def _unbpe(b):
    while b[:4] == b'BPE ':
        b = svrfmt.bpe_decode(b)
    return b


def leaves(path):
    data = open(path, 'rb').read()
    _, groups, _ = svrfmt.epac_read(data)
    out = []

    def walk(blob, key, depth=0):
        blob = _unbpe(blob)
        if blob[:4] == b'PACH' and depth < 6:
            try:
                sub = svrfmt.pach_read(blob)
            except Exception:
                out.append((key, blob))
                return
            for i, b in sub:
                walk(b, f'{key}/{i}', depth + 1)
        else:
            out.append((key, blob))

    for typ, ents in groups:
        for name, blob in ents:
            walk(blob, f'{typ.decode("latin1")}/{name.decode("latin1")}')
    return out


def fnv1a64(b):
    h = 0xcbf29ce484222325
    for c in b:
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def dds_info(d):
    h, w = struct.unpack_from('<II', d, 12)
    fourcc = d[84:88]
    bpb = {b'DXT1': 8, b'DXT3': 16, b'DXT5': 16}.get(fourcc)
    return w, h, fourcc.decode('latin1'), bpb


def content_key(d):
    w, h, fmt, bpb = dds_info(d)
    n = ((w + 3) // 4) * ((h + 3) // 4) * bpb
    return fnv1a64(d[128:128 + n])


def textures(path):
    """(name, DDS bytes) for every texture in a pac: DDS leaves and the 'count + 32-byte
    entries' texture lists (name[16], ext[4], size, offset, pad)."""
    tag = os.path.basename(path).split('.')[0]
    for k, b in leaves(path):
        if b[:4] == b'DDS ':
            yield f"{tag}:{k}", b
            continue
        if len(b) < 48:
            continue
        n = struct.unpack_from('<I', b, 0)[0]
        if not (0 < n < 4096) or 16 + 32 * n > len(b):
            continue
        for i in range(n):
            e = 16 + 32 * i
            name = b[e:e + 16].split(b'\0')[0].decode('latin1', 'replace')
            ext = b[e + 16:e + 20].split(b'\0')[0]
            size, off = struct.unpack_from('<II', b, e + 20)
            if ext != b'dds' or off + size > len(b) or b[off:off + 4] != b'DDS ':
                continue
            yield f"{tag}:{k}/{name}", b[off:off + size]

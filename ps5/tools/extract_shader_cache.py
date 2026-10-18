#!/usr/bin/env python3
"""[ps5 port] The game's recompiled shaders, from the SvR 2008 PC release.

The PC port builds generated/native/shader_cache.cpp with XenosRecomp from shaders dumped
while playing, and ships them inside its Vulkan program (svr2008_vulkan.exe): a table of
ShaderCacheEntry (hash, DXIL offset/size, SPIR-V offset/size, spec constant mask, pointer)
and the SPIR-V (smol-v) compressed with zstd. This writes the same shader_cache.cpp from
that program, SPIR-V only, so the PS5 build draws with the same shaders.

    extract_shader_cache.py <SVR2008-NATIVE.zip or svr2008_vulkan.exe> <out shader_cache.cpp>
"""
import struct, sys, zipfile
import zstandard

src, out = sys.argv[1], sys.argv[2]
if src.lower().endswith('.zip'):
    exe = zipfile.ZipFile(src).read('svr2008_vulkan.exe')
else:
    exe = open(src, 'rb').read()

# zstd frames in the program, decompressed
frames = []
i = 0
while (i := exe.find(b'\x28\xb5\x2f\xfd', i)) >= 0:
    try:
        data = zstandard.ZstdDecompressor().decompressobj().decompress(exe[i:i + 64_000_000])
        frames.append((i, data))
    except zstandard.ZstdError:
        pass
    i += 4

# the entry table: consecutive 40-byte records whose offsets follow on from each other
REC = 40
def table_at(p):
    n, dx, sp = 0, 0, 0
    while p + REC <= len(exe):
        h, dxo, dxs, spo, sps, mask = struct.unpack_from('<QIIIII', exe, p)
        if dxo != dx or spo != sp or sps == 0:
            break
        dx += dxs; sp += sps; n += 1; p += REC
    return n, dx, sp

best = None
for p in range(0, len(exe) - REC * 4, 8):
    h, dxo, dxs, spo, sps, mask = struct.unpack_from('<QIIIII', exe, p)
    if dxo or spo or not sps:
        continue
    n, dx, sp = table_at(p)
    if n > 16 and any(len(d) == sp for _, d in frames):
        best = (p, n, sp)
        break
if not best:
    sys.exit('no shader table found')
p, n, sp_total = best
spirv = next(d for _, d in frames if len(d) == sp_total)
entries = [struct.unpack_from('<QIIIII', exe, p + k * REC) for k in range(n)]
print(f'{n} shaders, {len(spirv)} bytes of SPIR-V (smol-v)')

blob = zstandard.ZstdCompressor(level=19).compress(spirv)
with open(out, 'w') as f:
    f.write('// [ps5 port] From the SvR 2008 PC release (ps5/tools/extract_shader_cache.py). Do not edit.\n')
    f.write('#include "gpu/shaders/shader_cache.h"\n')
    f.write('ShaderCacheEntry g_shaderCacheEntries[] = {\n')
    for h, dxo, dxs, spo, sps, mask in entries:
        f.write(f'\t{{ 0x{h:X}, 0, 0, {spo}, {sps}, {mask} }},\n')
    f.write('};\n')
    f.write('const uint8_t g_compressedDxilCache[] = {0};\n')
    f.write('const size_t g_dxilCacheCompressedSize = 0;\n')
    f.write('const size_t g_dxilCacheDecompressedSize = 0;\n')
    f.write('const uint8_t g_compressedSpirvCache[] = {')
    for k in range(0, len(blob), 4096):
        f.write(','.join(str(b) for b in blob[k:k + 4096]) + ',\n')
    f.write('};\n')
    f.write(f'const size_t g_spirvCacheCompressedSize = {len(blob)};\n')
    f.write(f'const size_t g_spirvCacheDecompressedSize = {len(spirv)};\n')
    f.write(f'const size_t g_shaderCacheEntryCount = {n};\n')
print(f'wrote {out}')

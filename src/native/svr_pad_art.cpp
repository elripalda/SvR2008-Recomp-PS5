// SvR 2008: PlayStation button pictures (see svr_pad_art.h). The edits are the same as
// ps5/tools/ps_buttons/ops.py, which makes the previews.

#include "svr_pad_art.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/xenos.h>

#include "core/logging.h"
#include "gpu/texture_upload.h"

REXCVAR_DEFINE_BOOL(svr_ps_buttons, true, "SvR",
                    "PlayStation button pictures in the menus and matches (off: the game's Xbox ones)");

namespace bd::gpu {
namespace {

namespace xe = rex::graphics::xenos;

struct Glyph {
  u32 w = 0, h = 0;
  bool alpha_only = false;
  const u8 *px = nullptr;
};

struct Op {
  u8 kind;   // 0 clear, 1 paste, 2 fill, 3 copy
  u8 flags;  // copy: 1 = the light lettering only
  u16 glyph;
  i16 x, y, w, h, sx, sy;
};

struct Edit {
  u32 w = 0, h = 0, format = 0;
  u32 first = 0, count = 0;
  std::string name;
};

struct Pack {
  std::vector<u8> data;
  std::vector<Glyph> glyphs;
  std::vector<Op> ops;
  std::unordered_map<u64, Edit> edits;
  std::unordered_set<u64> shapes;  // (w, h, format) of the edited textures
};

Pack g_pack;
bool g_loaded = false;
std::mutex g_cache_mutex;
std::unordered_map<u64, std::vector<u8>> g_cache;  // key -> edited RGBA8 (rows w * 4)
std::unordered_set<u64> g_logged;

u64 Shape(u32 w, u32 h, u32 fmt) { return (u64(w) << 32) | (u64(h) << 8) | fmt; }

template <typename T>
T Rd(const u8 *p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}

// ---------------------------------------------------------------- BC decode

void Rgb565(u16 c, int out[3]) {
  out[0] = ((c >> 11) & 31) * 255 / 31;
  out[1] = ((c >> 5) & 63) * 255 / 63;
  out[2] = (c & 31) * 255 / 31;
}

// One 4x4 colour block into rgba (stride in bytes); alpha left as is unless 3-colour BC1.
void DecodeColor(const u8 *b, u8 *rgba, size_t stride, bool four_always) {
  const u16 c0 = Rd<u16>(b), c1 = Rd<u16>(b + 2);
  int p[4][4];
  Rgb565(c0, p[0]);
  Rgb565(c1, p[1]);
  const bool four = four_always || c0 > c1;
  for (int k = 0; k < 3; ++k) {
    if (four) {
      p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
      p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
    } else {
      p[2][k] = (p[0][k] + p[1][k]) / 2;
      p[3][k] = 0;
    }
  }
  p[0][3] = p[1][3] = p[2][3] = 255;
  p[3][3] = four ? 255 : 0;
  const u32 idx = Rd<u32>(b + 4);
  for (int i = 0; i < 16; ++i) {
    const int s = (idx >> (2 * i)) & 3;
    u8 *o = rgba + (i / 4) * stride + (i % 4) * 4;
    o[0] = u8(p[s][0]);
    o[1] = u8(p[s][1]);
    o[2] = u8(p[s][2]);
    if (!four_always) o[3] = u8(p[s][3]);
  }
}

void DecodeAlphaBC3(const u8 *b, u8 *rgba, size_t stride) {
  const int a0 = b[0], a1 = b[1];
  int a[8] = {a0, a1};
  if (a0 > a1) {
    for (int i = 1; i < 7; ++i) a[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i < 5; ++i) a[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    a[6] = 0;
    a[7] = 255;
  }
  u64 bits = 0;
  for (int i = 0; i < 6; ++i) bits |= u64(b[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) rgba[(i / 4) * stride + (i % 4) * 4 + 3] = u8(a[(bits >> (3 * i)) & 7]);
}

void DecodeAlphaBC2(const u8 *b, u8 *rgba, size_t stride) {
  for (int i = 0; i < 16; ++i) {
    const int v = (b[i / 2] >> ((i & 1) * 4)) & 15;
    rgba[(i / 4) * stride + (i % 4) * 4 + 3] = u8(v * 17);
  }
}

// The top level (w x h, any size) as RGBA8, rows w * 4.
std::vector<u8> Decode(u32 w, u32 h, u32 fmt, const u8 *src, u32 row_bytes) {
  const u32 bw = (w + 3) / 4, bh = (h + 3) / 4;
  const u32 pw = bw * 4, ph = bh * 4;
  std::vector<u8> full(size_t(pw) * ph * 4, 255);
  const size_t stride = size_t(pw) * 4;
  const u32 bpb = fmt == 1 ? 8 : 16;
  for (u32 by = 0; by < bh; ++by) {
    for (u32 bx = 0; bx < bw; ++bx) {
      const u8 *b = src + size_t(by) * row_bytes + size_t(bx) * bpb;
      u8 *o = full.data() + size_t(by) * 4 * stride + size_t(bx) * 16;
      if (fmt == 1) {
        DecodeColor(b, o, stride, false);
      } else {
        DecodeColor(b + 8, o, stride, true);
        if (fmt == 3)
          DecodeAlphaBC2(b, o, stride);
        else
          DecodeAlphaBC3(b, o, stride);
      }
    }
  }
  std::vector<u8> out(size_t(w) * h * 4);
  for (u32 y = 0; y < h; ++y) std::memcpy(&out[size_t(y) * w * 4], &full[size_t(y) * stride], size_t(w) * 4);
  return out;
}

// ---------------------------------------------------------------- the edits

struct Image {
  u32 w, h;
  std::vector<u8> px;  // RGBA8, straight alpha
  u8 *at(int x, int y) { return &px[(size_t(y) * w + x) * 4]; }
  const u8 *at(int x, int y) const { return &px[(size_t(y) * w + x) * 4]; }
};

void Clear(Image &img, int x, int y, int w, int h) {
  for (int j = std::max(0, y); j < std::min<int>(img.h, y + h); ++j)
    for (int i = std::max(0, x); i < std::min<int>(img.w, x + w); ++i) img.at(i, j)[3] = 0;
}

// "Over", straight alpha. src(sx, sy) gives a pixel of the source.
template <typename Src>
void Over(Image &img, int x, int y, int w, int h, Src src) {
  for (int j = std::max(0, y); j < std::min<int>(img.h, y + h); ++j) {
    for (int i = std::max(0, x); i < std::min<int>(img.w, x + w); ++i) {
      u8 s[4];
      src(i - x, j - y, s);
      u8 *d = img.at(i, j);
      const float sa = s[3] / 255.0f, da = d[3] / 255.0f;
      const float oa = sa + da * (1.0f - sa);
      for (int c = 0; c < 3; ++c) {
        const float v = oa > 0 ? (s[c] * sa + d[c] * da * (1.0f - sa)) / oa : 0.0f;
        d[c] = u8(std::clamp(v + 0.5f, 0.0f, 255.0f));
      }
      d[3] = u8(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
    }
  }
}

void Paste(Image &img, const Glyph &g, int x, int y) {
  Over(img, x, y, int(g.w), int(g.h), [&](int i, int j, u8 *s) {
    if (g.alpha_only) {
      s[0] = s[1] = s[2] = 255;
      s[3] = g.px[size_t(j) * g.w + i];
    } else {
      std::memcpy(s, g.px + (size_t(j) * g.w + i) * 4, 4);
    }
  });
}

// Push-pull fill: the masked pixels take a smooth blend of the picture around them.
void Fill(Image &img, const Glyph &mask, int mx, int my) {
  const int W = int(img.w), H = int(img.h);
  struct Level {
    int w, h;
    std::vector<float> c;  // rgba * k
    std::vector<float> k;
  };
  std::vector<Level> levels;
  Level l0{W, H, std::vector<float>(size_t(W) * H * 4), std::vector<float>(size_t(W) * H)};
  std::vector<u8> m(size_t(W) * H, 0);
  for (u32 j = 0; j < mask.h; ++j)
    for (u32 i = 0; i < mask.w; ++i) {
      const int x = mx + int(i), y = my + int(j);
      if (x >= 0 && y >= 0 && x < W && y < H && mask.px[size_t(j) * mask.w + i] > 127) m[size_t(y) * W + x] = 1;
    }
  for (int p = 0; p < W * H; ++p) {
    const float k = m[p] ? 0.0f : 1.0f;
    l0.k[p] = k;
    for (int c = 0; c < 4; ++c) l0.c[size_t(p) * 4 + c] = img.px[size_t(p) * 4 + c] * k;
  }
  levels.push_back(std::move(l0));
  while (std::min(levels.back().w, levels.back().h) > 1) {
    const Level &f = levels.back();
    Level n{(f.w + 1) / 2, (f.h + 1) / 2, {}, {}};
    n.c.assign(size_t(n.w) * n.h * 4, 0.0f);
    n.k.assign(size_t(n.w) * n.h, 0.0f);
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x) {
        const size_t s = size_t(y) * f.w + x, d = size_t(y / 2) * n.w + x / 2;
        n.k[d] += f.k[s];
        for (int c = 0; c < 4; ++c) n.c[d * 4 + c] += f.c[s * 4 + c];
      }
    for (size_t d = 0; d < n.k.size(); ++d) {
      const float k = n.k[d];
      const float scale = (k > 1e-6f ? 1.0f / k : 0.0f) * std::min(k, 1.0f);
      for (int c = 0; c < 4; ++c) n.c[d * 4 + c] *= scale;
      n.k[d] = std::min(k, 1.0f);
    }
    levels.push_back(std::move(n));
  }
  // up: colour where known, else the coarser level (upsampled, 3x3 box)
  std::vector<float> up;
  {
    const Level &t = levels.back();
    up.resize(size_t(t.w) * t.h * 4);
    for (size_t p = 0; p < t.k.size(); ++p)
      for (int c = 0; c < 4; ++c) up[p * 4 + c] = t.k[p] > 1e-6f ? t.c[p * 4 + c] / t.k[p] : 0.0f;
  }
  for (int li = int(levels.size()) - 2; li >= 0; --li) {
    const Level &f = levels[li];
    const Level &coarse = levels[li + 1];
    std::vector<float> u(size_t(f.w) * f.h * 4);
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x)
        for (int c = 0; c < 4; ++c) u[(size_t(y) * f.w + x) * 4 + c] = up[(size_t(y / 2) * coarse.w + x / 2) * 4 + c];
    std::vector<float> nu(u.size());
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x)
        for (int c = 0; c < 4; ++c) {
          float s = 0.0f;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              const int xx = std::clamp(x + dx, 0, f.w - 1), yy = std::clamp(y + dy, 0, f.h - 1);
              s += u[(size_t(yy) * f.w + xx) * 4 + c];
            }
          nu[(size_t(y) * f.w + x) * 4 + c] = s / 9.0f;
        }
    for (size_t p = 0; p < f.k.size(); ++p) {
      const float k = f.k[p];
      for (int c = 0; c < 4; ++c) {
        const float cur = k > 1e-6f ? f.c[p * 4 + c] / k : 0.0f;
        nu[p * 4 + c] = k >= 0.999f ? cur : cur * k + nu[p * 4 + c] * (1.0f - k);
      }
    }
    up = std::move(nu);
  }
  for (int p = 0; p < W * H; ++p)
    if (m[p])
      for (int c = 0; c < 4; ++c) img.px[size_t(p) * 4 + c] = u8(std::clamp(up[size_t(p) * 4 + c], 0.0f, 255.0f));
}

void Copy(Image &img, const Image &orig, const Op &op) {
  const int w = op.w, h = op.h;
  std::vector<u8> block(size_t(w) * h * 4, 0);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      const int x = op.x + i, y = op.y + j;
      if (x >= 0 && y >= 0 && x < int(orig.w) && y < int(orig.h)) std::memcpy(&block[(size_t(j) * w + i) * 4], orig.at(x, y), 4);
    }
  if (op.flags & 1) {
    // the light lettering and its dark edge (lettering grown by 2 px) only
    std::vector<float> a(size_t(w) * h);
    for (size_t p = 0; p < a.size(); ++p) {
      const u8 *q = &block[p * 4];
      const float luma = q[0] * 0.299f + q[1] * 0.587f + q[2] * 0.114f;
      a[p] = std::clamp((luma - 150.0f) / 60.0f, 0.0f, 1.0f);
    }
    for (int it = 0; it < 2; ++it) {
      std::vector<float> g(a);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          float v = a[size_t(y) * w + x];
          if (x > 0) v = std::max(v, a[size_t(y) * w + x - 1]);
          if (x + 1 < w) v = std::max(v, a[size_t(y) * w + x + 1]);
          if (y > 0) v = std::max(v, a[size_t(y - 1) * w + x]);
          if (y + 1 < h) v = std::max(v, a[size_t(y + 1) * w + x]);
          g[size_t(y) * w + x] = v;
        }
      a.swap(g);
    }
    for (size_t p = 0; p < a.size(); ++p) block[p * 4 + 3] = u8(a[p] * 255.0f);
    Over(img, op.sx, op.sy, w, h, [&](int i, int j, u8 *s) { std::memcpy(s, &block[(size_t(j) * w + i) * 4], 4); });
  } else {
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) {
        const int x = op.sx + i, y = op.sy + j;
        if (x >= 0 && y >= 0 && x < int(img.w) && y < int(img.h)) std::memcpy(img.at(x, y), &block[(size_t(j) * w + i) * 4], 4);
      }
  }
}

// Transparent pixels next to visible ones take their colour (two pixels deep), so filtering at
// the edges doesn't pull in what was there before (or black). Same as ops.py's bleed.
void Bleed(Image &img) {
  const int W = int(img.w), H = int(img.h);
  std::vector<float> a(size_t(W) * H * 4);
  for (size_t i = 0; i < a.size(); ++i) a[i] = img.px[i];
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<float> next(a);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const size_t p = size_t(y) * W + x;
        if (a[p * 4 + 3] != 0.0f) continue;
        float s[3] = {0, 0, 0}, sw = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            const size_t q = size_t(yy) * W + xx;
            const float w = a[q * 4 + 3];
            if (w <= 0.0f) continue;
            for (int c = 0; c < 3; ++c) s[c] += a[q * 4 + c] * w;
            sw += w;
          }
        if (sw > 0) {
          for (int c = 0; c < 3; ++c) next[p * 4 + c] = s[c] / sw;
          next[p * 4 + 3] = 0.001f;
        }
      }
    a.swap(next);
  }
  for (size_t p = 0; p < size_t(W) * H; ++p) {
    for (int c = 0; c < 3; ++c) img.px[p * 4 + c] = u8(std::clamp(a[p * 4 + c] + 0.5f, 0.0f, 255.0f));
    img.px[p * 4 + 3] = a[p * 4 + 3] < 0.5f ? 0 : u8(std::clamp(a[p * 4 + 3] + 0.5f, 0.0f, 255.0f));
  }
}

std::vector<u8> Apply(const Edit &e, std::vector<u8> rgba) {
  Image img{e.w, e.h, std::move(rgba)};
  const Image orig = img;
  for (u32 i = 0; i < e.count; ++i) {
    const Op &op = g_pack.ops[e.first + i];
    switch (op.kind) {
    case 0:
      Clear(img, op.x, op.y, op.w, op.h);
      break;
    case 1:
      if (op.glyph < g_pack.glyphs.size()) Paste(img, g_pack.glyphs[op.glyph], op.x, op.y);
      break;
    case 2:
      if (op.glyph < g_pack.glyphs.size()) Fill(img, g_pack.glyphs[op.glyph], op.x, op.y);
      break;
    case 3:
      Copy(img, orig, op);
      break;
    default:
      break;
    }
  }
  Bleed(img);
  return std::move(img.px);
}

u64 Fnv1a64(const u8 *rows, u32 row_count, u32 row_len, u32 stride) {
  u64 h = 0xcbf29ce484222325ull;
  for (u32 r = 0; r < row_count; ++r) {
    const u8 *p = rows + size_t(r) * stride;
    for (u32 i = 0; i < row_len; ++i) {
      h ^= p[i];
      h *= 0x100000001b3ull;
    }
  }
  return h;
}

u32 PackFormat(u32 xenos_format) {
  switch (xe::TextureFormat(xenos_format)) {
  case xe::TextureFormat::k_DXT1:
    return 1;
  case xe::TextureFormat::k_DXT2_3:
    return 3;
  case xe::TextureFormat::k_DXT4_5:
    return 5;
  default:
    return 0;
  }
}

}  // namespace

bool SvrPadArtLoad(const std::filesystem::path &file) {
  if (!REXCVAR_GET(svr_ps_buttons)) {
    BD_INFO("[svr] button pictures: the game's own (setting off)");
    return false;
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    BD_WARN("[svr] button pictures: {} missing; the game's own", file.string());
    return false;
  }
  Pack pack;
  pack.data.assign(std::istreambuf_iterator<char>(in), {});
  const u8 *d = pack.data.data();
  const size_t n = pack.data.size();
  if (n < 24 || std::memcmp(d, "SVRPAD01", 8) != 0) {
    BD_WARN("[svr] button pictures: {} isn't a button picture pack", file.string());
    return false;
  }
  const u32 glyphs = Rd<u32>(d + 8), edits = Rd<u32>(d + 12), ops = Rd<u32>(d + 16), names = Rd<u32>(d + 20);
  const size_t gtab = 24, etab = gtab + size_t(glyphs) * 12, otab = etab + size_t(edits) * 24;
  const size_t ntab = otab + size_t(ops) * 16, pix = ntab + names;
  if (pix > n) {
    BD_WARN("[svr] button pictures: {} is cut short", file.string());
    return false;
  }
  for (u32 i = 0; i < glyphs; ++i) {
    const u8 *g = d + gtab + size_t(i) * 12;
    Glyph gl;
    gl.w = Rd<u16>(g);
    gl.h = Rd<u16>(g + 2);
    gl.alpha_only = g[4] == 1;
    const size_t off = pix + Rd<u32>(g + 8);
    const size_t bytes = size_t(gl.w) * gl.h * (gl.alpha_only ? 1 : 4);
    if (off + bytes > n) {
      BD_WARN("[svr] button pictures: picture {} out of range", i);
      return false;
    }
    gl.px = d + off;
    pack.glyphs.push_back(gl);
  }
  for (u32 i = 0; i < ops; ++i) {
    const u8 *o = d + otab + size_t(i) * 16;
    Op op;
    op.kind = o[0];
    op.flags = o[1];
    op.glyph = Rd<u16>(o + 2);
    op.x = Rd<i16>(o + 4);
    op.y = Rd<i16>(o + 6);
    op.w = Rd<i16>(o + 8);
    op.h = Rd<i16>(o + 10);
    op.sx = Rd<i16>(o + 12);
    op.sy = Rd<i16>(o + 14);
    pack.ops.push_back(op);
  }
  for (u32 i = 0; i < edits; ++i) {
    const u8 *e = d + etab + size_t(i) * 24;
    Edit ed;
    const u64 key = Rd<u64>(e);
    ed.w = Rd<u16>(e + 8);
    ed.h = Rd<u16>(e + 10);
    ed.format = e[12];
    ed.count = Rd<u16>(e + 14);
    ed.first = Rd<u32>(e + 16);
    const u32 name = Rd<u32>(e + 20);
    if (ed.first + ed.count > ops) continue;
    if (name < names) ed.name = reinterpret_cast<const char *>(d + ntab + name);
    pack.shapes.insert(Shape(ed.w, ed.h, ed.format));
    pack.edits.emplace(key, std::move(ed));
  }
  // (glyph pointers point into data: moving the vector keeps its buffer)
  g_pack = std::move(pack);
  g_loaded = true;
  BD_INFO("[svr] button pictures: PlayStation ({} textures, {} pictures)", g_pack.edits.size(),
          g_pack.glyphs.size());
  return true;
}

GuestTexture *SvrPadArtReplace(u32 width, u32 height, u32 format, const u8 *staging,
                               u32 staging_row_bytes) {
  if (!g_loaded || !staging)
    return nullptr;
  const u32 fmt = PackFormat(format);
  if (!fmt || !g_pack.shapes.count(Shape(width, height, fmt)))
    return nullptr;
  const u32 bw = (width + 3) / 4, bh = (height + 3) / 4, bpb = fmt == 1 ? 8 : 16;
  const u64 key = Fnv1a64(staging, bh, bw * bpb, staging_row_bytes);
  const auto it = g_pack.edits.find(key);
  if (it == g_pack.edits.end())
    return nullptr;
  const Edit &e = it->second;
  std::vector<u8> rgba;
  {
    std::lock_guard lock(g_cache_mutex);
    auto c = g_cache.find(key);
    if (c == g_cache.end()) {
      c = g_cache.emplace(key, Apply(e, Decode(width, height, fmt, staging, staging_row_bytes))).first;
      if (g_logged.insert(key).second)
        BD_INFO("[svr] button pictures: {} ({}x{})", e.name, width, height);
    }
    rgba = c->second;
  }
  // rows padded to 256 bytes for the upload
  const u32 row = (width * 4 + 255u) & ~255u;
  std::vector<u8> up(size_t(row) * height, 0);
  for (u32 y = 0; y < height; ++y) std::memcpy(&up[size_t(y) * row], &rgba[size_t(y) * width * 4], size_t(width) * 4);
  GuestTexture *tex = BuildBCMirrorTexture(width, height, u32(xe::TextureFormat::k_8_8_8_8), up.data(), up.size(),
                                           row, row / 4u);
  if (!tex)
    BD_WARN("[svr] button pictures: upload failed for {}", e.name);
  return tex;
}

}  // namespace bd::gpu

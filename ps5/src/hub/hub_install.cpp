// WWE SmackDown vs. Raw 2008 - the PS5 hub's installer (see hub_install.h).
// The disc image's XDVDFS file system is read as the SmackDown vs. Raw 2011
// port's installer reads it.
#include "hub_install.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace svr2008::hub::install {

namespace {

constexpr uint32_t kTitleId = 0x5451080B;  // the game's (default.xex execution info)
// The default.xex this app's code was made from (WWE-SVR2008-RECOMP's).
constexpr uint64_t kXexSize = 9515008;
constexpr uint32_t kXexCrc = 0xF96DB5B1;
constexpr size_t kChunk = 8u << 20;

// ------------------------------------------------------------------ helpers

uint32_t g_crc[8][256];

void BuildCrc() {
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int j = 0; j < 8; ++j) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    g_crc[0][i] = c;
  }
  for (uint32_t i = 0; i < 256; ++i)
    for (int j = 1; j < 8; ++j) g_crc[j][i] = (g_crc[j - 1][i] >> 8) ^ g_crc[0][g_crc[j - 1][i] & 0xFF];
}

void CrcInit() {
  static std::once_flag once;
  std::call_once(once, BuildCrc);
}

// zlib's CRC-32, 8 bytes at a time. `c`: the CRC so far (0 to start).
uint32_t Crc(uint32_t c, const uint8_t* p, size_t n) {
  c = ~c;
  while (n >= 8) {
    const uint32_t a = c ^ (uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
    const uint32_t b = uint32_t(p[4]) | uint32_t(p[5]) << 8 | uint32_t(p[6]) << 16 | uint32_t(p[7]) << 24;
    c = g_crc[7][a & 0xFF] ^ g_crc[6][(a >> 8) & 0xFF] ^ g_crc[5][(a >> 16) & 0xFF] ^ g_crc[4][a >> 24] ^
        g_crc[3][b & 0xFF] ^ g_crc[2][(b >> 8) & 0xFF] ^ g_crc[1][(b >> 16) & 0xFF] ^ g_crc[0][b >> 24];
    p += 8, n -= 8;
  }
  while (n--) c = g_crc[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
  return ~c;
}

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t Le64(const uint8_t* p) { return uint64_t(Le32(p)) | uint64_t(Le32(p + 4)) << 32; }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool EndsWith(const std::string& s, const char* tail) {
  const size_t n = std::strlen(tail);
  return s.size() >= n && Lower(s.substr(s.size() - n)) == tail;
}

std::string ErrnoText(int e) { return std::strerror(e); }

// A file opened for reading at any offset.
class Reader {
 public:
  explicit Reader(const fs::path& p) : fd_(::open(p.c_str(), O_RDONLY)) {}
  ~Reader() {
    if (fd_ >= 0) ::close(fd_);
  }
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  bool ok() const { return fd_ >= 0; }
  bool Read(uint64_t off, void* buf, size_t n) const {
    auto* p = static_cast<uint8_t*>(buf);
    while (n) {
      const ssize_t got = ::pread(fd_, p, n, off_t(off));
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) return false;
      p += got, off += uint64_t(got), n -= size_t(got);
    }
    return true;
  }
  uint64_t Size() const {
    struct stat st {};
    return fd_ >= 0 && ::fstat(fd_, &st) == 0 ? uint64_t(st.st_size) : 0;
  }

 private:
  int fd_ = -1;
};

// A file being written: <name>.part, renamed to <name> when complete.
class Writer {
 public:
  explicit Writer(fs::path dst) : dst_(std::move(dst)), part_(dst_.string() + ".part") {
    std::error_code ec;
    fs::create_directories(dst_.parent_path(), ec);
    fd_ = ::open(part_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) error_ = "couldn't create " + dst_.filename().string() + " (" + ErrnoText(errno) + ")";
  }
  ~Writer() {
    if (fd_ >= 0) ::close(fd_);
    if (!done_) ::unlink(part_.c_str());
  }
  bool Write(const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (n && fd_ >= 0) {
      const ssize_t put = ::write(fd_, p, n);
      if (put < 0 && errno == EINTR) continue;
      if (put <= 0) {
        error_ = "couldn't write " + dst_.filename().string() + " (" + ErrnoText(put < 0 ? errno : ENOSPC) +
                 "). Is the storage full?";
        return false;
      }
      p += put, n -= size_t(put);
    }
    return fd_ >= 0;
  }
  bool Finish() {
    if (fd_ < 0) return false;
    const int rc = ::close(fd_);
    fd_ = -1;
    if (rc != 0) {
      error_ = "couldn't write " + dst_.filename().string() + " (" + ErrnoText(errno) + ")";
      return false;
    }
    std::error_code ec;
    fs::rename(part_, dst_, ec);
    if (ec) {
      error_ = "couldn't rename " + part_.filename().string() + " (" + ec.message() + ")";
      return false;
    }
    done_ = true;
    return true;
  }
  const std::string& error() const { return error_; }

 private:
  fs::path dst_, part_;
  int fd_ = -1;
  bool done_ = false;
  std::string error_;
};

// A file's CRC-32 (0 if it can't be read).
uint32_t FileCrc(const fs::path& p);

uint64_t FileSize(const fs::path& p) {
  std::error_code ec;
  const auto n = fs::file_size(p, ec);
  return ec ? uint64_t(-1) : uint64_t(n);
}

uint32_t FileCrc(const fs::path& p) {
  CrcInit();
  Reader f(p);
  if (!f.ok()) return 0;
  std::vector<uint8_t> buf(4u << 20);
  uint32_t c = 0;
  const uint64_t size = f.Size();
  for (uint64_t at = 0; at < size;) {
    const size_t n = size_t(std::min<uint64_t>(size - at, buf.size()));
    if (!f.Read(at, buf.data(), n)) return 0;
    c = Crc(c, buf.data(), n);
    at += n;
  }
  return c;
}

// ------------------------------------------------------------ the disc image

struct DiscEntry {
  std::string path;  // relative, '/'
  uint32_t sector = 0, size = 0;
  bool dir = false;
};

struct Disc {
  std::unique_ptr<Reader> f;
  uint64_t base = 0;  // the game partition's offset in the image
  std::vector<DiscEntry> entries;

  bool Read(uint64_t off, void* buf, size_t n) const { return f->Read(base + off, buf, n); }

  // One directory table: u16 left, u16 right, u32 sector, u32 size, u8
  // attributes, u8 name length, name; 4-byte aligned; 0xFFFF where an entry
  // would start pads to the next sector.
  bool Dir(uint32_t sector, uint32_t size, const std::string& prefix, int depth) {
    if (depth > 32 || size > (16u << 20) || entries.size() > 100000) return false;
    std::vector<uint8_t> buf(size);
    if (!Read(uint64_t(sector) * 2048, buf.data(), size)) return false;
    uint32_t off = 0;
    while (off + 14 <= size) {
      const uint8_t* e = buf.data() + off;
      const uint16_t left = Le16(e);
      const uint8_t attr = e[12], nlen = e[13];
      if (left == 0xFFFF || nlen == 0) {
        off = (off / 2048 + 1) * 2048;
        continue;
      }
      if (off + 14 + nlen > size) return false;
      const std::string name(reinterpret_cast<const char*>(e + 14), nlen);
      if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos ||
          name.find('\0') != std::string::npos)
        return false;  // (unsafe, or broken)
      DiscEntry d;
      d.path = prefix.empty() ? name : prefix + "/" + name;
      d.sector = Le32(e + 4);
      d.size = Le32(e + 8);
      d.dir = attr & 0x10;
      entries.push_back(d);
      if (d.dir && d.size && !Dir(d.sector, d.size, d.path, depth + 1)) return false;
      off = (off + 14 + nlen + 3) & ~3u;
    }
    return true;
  }

  const DiscEntry* Find(const std::string& path) const {
    for (const DiscEntry& e : entries)
      if (!e.dir && Lower(e.path) == Lower(path)) return &e;
    return nullptr;
  }

  // The title ID in default.xex's execution info (optional header 0x00040006).
  bool TitleId(const DiscEntry& x, uint32_t* id) const {
    uint8_t hdr[24], opt[64 * 8], exe[16];
    const uint64_t at = uint64_t(x.sector) * 2048;
    if (x.size < 24 || !Read(at, hdr, 24) || std::memcmp(hdr, "XEX2", 4)) return false;
    const uint32_t n = Be32(hdr + 20);
    if (n == 0 || n > 64 || 24 + n * 8 > x.size || !Read(at + 24, opt, n * 8)) return false;
    for (uint32_t i = 0; i < n; ++i) {
      if (Be32(opt + i * 8) != 0x00040006u) continue;
      const uint32_t o = Be32(opt + i * 8 + 4);
      if (o + 16 > x.size || !Read(at + o, exe, 16)) return false;
      *id = Be32(exe + 12);
      return true;
    }
    return false;
  }
};

// Opens an image (Redump ISO or XISO) and lists its files; checks it's this
// game, and the default.xex this app was made from. `why` if it can't be used.
bool OpenDisc(const fs::path& image, Disc* d, DiscImage::State* state, std::string* why) {
  d->f = std::make_unique<Reader>(image);
  if (!d->f->ok()) {
    *state = DiscImage::kNotADisc;
    *why = "It can't be opened (" + ErrnoText(errno) + ").";
    return false;
  }
  static const uint64_t kBases[] = {0, 0x18300000ull, 0xFD90000ull, 0x2080000ull};
  for (uint64_t base : kBases) {
    d->base = base;
    uint8_t vd[2048];
    if (!d->Read(32 * 2048, vd, sizeof(vd)) || std::memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20)) continue;
    d->entries.clear();
    if (!d->Dir(Le32(vd + 20), Le32(vd + 24), "", 0)) {
      *state = DiscImage::kNotADisc;
      *why = "The image is damaged: its file list can't be read.";
      return false;
    }
    const DiscEntry* x = d->Find("default.xex");
    uint32_t tid = 0;
    if (!x || !d->TitleId(*x, &tid)) {
      *state = DiscImage::kOtherGame;
      *why = x ? "Its default.xex can't be read: the image may be incomplete." : "It isn't an Xbox 360 game disc.";
      return false;
    }
    if (tid != kTitleId) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "It's another game (title ID %08X).", tid);
      *state = DiscImage::kOtherGame;
      *why = buf;
      return false;
    }
    // (the app's code was made from one default.xex: another release of the
    // game - another region or version - doesn't run with it)
    CrcInit();
    std::vector<uint8_t> xex(x->size);
    if (x->size != kXexSize || !d->Read(uint64_t(x->sector) * 2048, xex.data(), xex.size()) ||
        Crc(0, xex.data(), xex.size()) != kXexCrc) {
      *state = DiscImage::kOtherVersion;
      *why = "It's another release of SmackDown vs. Raw 2008 (another region or version) than the one this app was "
             "made for: it won't run.";
      return false;
    }
    *state = DiscImage::kThisGame;
    return true;
  }
  *state = DiscImage::kNotADisc;
  *why = "It isn't an Xbox 360 disc image (no Xbox file system in it).";
  return false;
}

// Files a game folder doesn't need: the disc's system update and dashboard art.
bool Optional(const std::string& path) {
  const std::string l = Lower(path);
  return l.rfind("$systemupdate", 0) == 0 || l == "nxeart";
}

// Folders the app and the game keep: not looked in.
bool Skipped(const std::string& name) {
  static const std::set<std::string> kSkip = {"assets", "userdata", "logs", "hub", "pad_icons", "savebackups",
                                              "sce_sys", "sce_module", "cache", "$systemupdate", "pac"};
  return name.empty() || name[0] == '.' || kSkip.count(Lower(name));
}

// The marker of an install under way (removed when it's done).
fs::path Marker(const fs::path& app) { return GameFolder(app) / ".installing"; }

}  // namespace

std::vector<fs::directory_entry> ListDir(const fs::path& dir) {
  std::vector<fs::directory_entry> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
       it.increment(ec))
    out.push_back(*it);
  return out;
}

std::string Bytes(uint64_t n) {
  char buf[32];
  if (n >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.1f GB", double(n) / double(1ull << 30));
  else if (n >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.0f MB", double(n) / double(1ull << 20));
  else std::snprintf(buf, sizeof(buf), "%.0f KB", double(n) / 1024.0);
  return buf;
}

fs::path GameFolder(const fs::path& app) { return app / "Assets"; }

std::vector<DiscImage> Scan(const fs::path& app) {
  std::vector<DiscImage> images;
  std::vector<fs::path> places = {app};
#if defined(__PROSPERO__)
  // (the internal storage, and USB / extended storage drives)
  places.push_back("/data");
  for (int i = 0; i < 8; ++i) places.push_back("/mnt/usb" + std::to_string(i));
  places.push_back("/mnt/ext0");
  places.push_back("/mnt/ext1");
#endif
  std::set<std::pair<std::string, uint64_t>> seen;  // (name, size): the same file reached twice
  std::error_code ec;
  auto look = [&](const fs::path& dir, int depth, auto& self) -> void {
    std::vector<fs::path> subdirs;
    for (const auto& e : ListDir(dir)) {
      const fs::path& p = e.path();
      const std::string name = p.filename().string();
      if (e.is_directory(ec)) {
        if (!Skipped(name) && depth == 0 && subdirs.size() < 64) subdirs.push_back(p);
        continue;
      }
      if (!e.is_regular_file(ec) || !(EndsWith(name, ".iso") || EndsWith(name, ".xiso"))) continue;
      const uint64_t size = e.file_size(ec);
      if (!seen.insert({name, size}).second) continue;
      DiscImage img;
      img.file = p;
      img.bytes = size;
      Disc d;
      if (OpenDisc(p, &d, &img.state, &img.note))
        for (const DiscEntry& de : d.entries)
          if (!de.dir && !Optional(de.path)) img.game_bytes += de.size, ++img.game_files;
      images.push_back(std::move(img));
    }
    for (const fs::path& s : subdirs) self(s, depth + 1, self);
  };
  for (const fs::path& place : places)
    if (fs::is_directory(place, ec)) look(place, 0, look);
  // the usable ones first
  std::stable_sort(images.begin(), images.end(), [](const DiscImage& a, const DiscImage& b) { return a.state < b.state; });
  return images;
}

State Look(const fs::path& app) {
  State s;
  const fs::path xex = GameFolder(app) / "default.xex";
  std::error_code ec;
  s.xex = fs::is_regular_file(xex, ec);
  s.incomplete = fs::exists(Marker(app), ec);
  if (s.xex) {
    // (checked once per file: by its size and time, then its content)
    static std::mutex m;
    static std::string last_key;
    static bool last_ok = false;
    const uint64_t size = FileSize(xex);
    struct stat st {};
    ::stat(xex.c_str(), &st);
    const std::string key = std::to_string(size) + ":" + std::to_string(int64_t(st.st_mtime));
    std::lock_guard l(m);
    if (key != last_key) last_key = key, last_ok = size == kXexSize && FileCrc(xex) == kXexCrc;
    s.xex_ok = last_ok;
  }
  return s;
}

namespace {

bool Fail(Progress& p, std::string why) {
  p.ok = false;
  p.result = std::move(why);
  return false;
}

bool CopyDisc(const fs::path& image, const fs::path& app, Progress& p) {
  Disc d;
  DiscImage::State st;
  std::string why;
  p.step = 0;
  p.SetFile("Reading the disc image");
  if (!OpenDisc(image, &d, &st, &why)) return Fail(p, image.filename().string() + ": " + why);
  const fs::path game = GameFolder(app);
  std::error_code ec;
  fs::create_directories(game, ec);
  std::ofstream(Marker(app)) << image.filename().string() << "\n";
  std::vector<const DiscEntry*> files;
  uint64_t need = 0;
  for (const DiscEntry& e : d.entries) {
    if (Optional(e.path)) continue;
    if (e.dir) {
      fs::create_directories(game / e.path, ec);
      continue;
    }
    if (FileSize(game / e.path) == e.size) continue;  // (there: an install that stopped goes on)
    files.push_back(&e);
    need += e.size;
  }
  const fs::space_info space = fs::space(game, ec);
  if (!ec && space.available < need + (64ull << 20))
    return Fail(p, "Not enough free space: the game needs " + Bytes(need + (64ull << 20)) + " and there is " +
                       Bytes(space.available) + ".");
  // (in the disc's order: the image is read front to back)
  std::sort(files.begin(), files.end(), [](const DiscEntry* a, const DiscEntry* b) { return a->sector < b->sector; });
  p.step = 1;
  p.total = need;
  p.done = 0;
  std::vector<uint8_t> buf(kChunk);
  for (const DiscEntry* e : files) {
    p.SetFile(e->path);
    Writer w(game / e->path);
    if (!w.error().empty()) return Fail(p, w.error());
    uint64_t off = uint64_t(e->sector) * 2048;
    for (uint64_t left = e->size; left;) {
      if (p.cancel) return Fail(p, "Stopped. The files copied so far are kept: install again to go on.");
      const size_t n = size_t(std::min<uint64_t>(left, kChunk));
      if (!d.Read(off, buf.data(), n)) return Fail(p, "The disc image can't be read at " + e->path + ": it may be incomplete.");
      if (!w.Write(buf.data(), n)) return Fail(p, w.error());
      off += n, left -= n;
      p.done += n;
    }
    if (!w.Finish()) return Fail(p, w.error());
  }
  return true;
}

}  // namespace

void Install(const fs::path& image, const fs::path& app, Progress& p) {
  CrcInit();
  p.ok = true;
  if (CopyDisc(image, app, p)) {
    p.step = 2;
    p.SetFile("default.xex");
    std::error_code ec;
    fs::remove(Marker(app), ec);
    const State s = Look(app);
    p.ok = s.Installed() && s.xex_ok;
    p.result = p.ok ? "The game is installed." : "The files are copied, but default.xex isn't the one this app needs.";
  }
  p.SetFile("");
  p.finished = true;
}

}  // namespace svr2008::hub::install

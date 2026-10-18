// WWE SmackDown vs. Raw 2008 PS5 - the PS5 hub's check of the game's files (see hub_install.h).
#include "hub_install.h"

#include <algorithm>
#include <unordered_map>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>

#include <fcntl.h>
#if __has_include(<rex/logging.h>)
#include <rex/logging.h>
#else  // (the hub preview on a PC)
#include <fmt/format.h>
#define REXLOG_INFO(...) std::fputs((fmt::format(__VA_ARGS__) + "\n").c_str(), stdout)
#define REXLOG_ERROR(...) std::fputs((fmt::format(__VA_ARGS__) + "\n").c_str(), stdout)
#endif
#include <sys/stat.h>
#include <unistd.h>

namespace svr2008::hub::install {

namespace {

// The executable this app's code was made from (the USA/Europe disc's default.xex).
constexpr uint64_t kXexSize = 9515008;
constexpr uint32_t kXexCrc = 0xF96DB5B1;

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


std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}



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

fs::path GameRoot(const fs::path& app) { return GameFolder(app); }

namespace {
// The game's executable in the game folder (the disc's name, or default.xex).
fs::path FindXex(const fs::path& root) {
  std::error_code ec;
  if (fs::is_regular_file(root / "default.xex", ec)) return root / "default.xex";
  return {};
}
}  // namespace

namespace {
struct GameFile {
  const char* path;
  uint64_t bytes;
  bool optional;
};
constexpr GameFile kGameFiles[] = {
#include "game_files.inc"
};
}  // namespace

std::vector<FileProblem> CheckGameFiles(const fs::path& app) {
  const fs::path root = GameRoot(app);
  // what's there (any letter case)
  std::unordered_map<std::string, uint64_t> have;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    const uint64_t bytes = it->file_size(e2);
    have[Lower(it->path().lexically_relative(root).generic_string())] = e2 ? 0 : bytes;
  }
  std::vector<FileProblem> problems;
  uint64_t total = 0;
  for (const GameFile& f : kGameFiles) {
    total += f.bytes;
    auto found = have.find(Lower(f.path));
    if (found == have.end()) {
      if (!f.optional) problems.push_back({f.path, f.bytes, 0, true});
    } else if (found->second != f.bytes) {
      problems.push_back({f.path, f.bytes, found->second, false});
    }
  }
  if (problems.empty()) {
    REXLOG_INFO("PS5 hub: game files: all {} there, whole ({} MB)", std::size(kGameFiles), total >> 20);
  } else {
    REXLOG_ERROR("PS5 hub: game files: {} of {} missing or not whole", problems.size(), std::size(kGameFiles));
    for (const FileProblem& p : problems)
      REXLOG_ERROR("PS5 hub:   {}: {}", p.path,
                   p.missing ? std::string("missing")
                             : fmt::format("{} of {} bytes", p.found, p.expected));
  }
  return problems;
}

// A disc image in the app folder (the game also starts from one).
bool HasDiscImage(const fs::path& app) {
  for (const auto& e : ListDir(app)) {
    std::error_code ec;
    std::string ext = Lower(e.path().extension().string());
    if (e.is_regular_file(ec) && (ext == ".iso" || ext == ".xiso")) return true;
  }
  return false;
}

State Look(const fs::path& app) {
  State s;
  const fs::path xex = FindXex(GameRoot(app));
  s.xex = !xex.empty();
  if (!s.xex && HasDiscImage(app)) {
    s.xex = s.xex_ok = true;  // (read from the image when the game starts)
    return s;
  }
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
    if (key != last_key) last_key = key, last_ok = size == kXexSize && (kXexCrc == 0 || FileCrc(xex) == kXexCrc);
    s.xex_ok = last_ok;
  }
  if (s.xex) s.problems = CheckGameFiles(app);
  return s;
}

}  // namespace svr2008::hub::install

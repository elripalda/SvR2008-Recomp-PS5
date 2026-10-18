// WWE SmackDown vs. Raw 2008 - PlayStation button pictures (see pad_icons.h).
#include "pad_icons.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

namespace fs = std::filesystem;

namespace svr2008 {
namespace {

// The archives that hold Xbox button textures (HD: the game runs at 720p).
constexpr const char* kPacs[] = {"game2dHD.pac", "menu/menuHD.pac", "evt/evt2dHD.pac", "menu/gm_mode.pac"};
constexpr char kStampVersion[] = "pad_icons v1";

uint64_t Fnv1a64(const uint8_t* p, size_t n) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ull;
  }
  return h;
}

uint32_t Rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct Replacement {
  fs::path file;
  uint64_t size = 0;
};
using Table = std::map<uint64_t, Replacement>;

struct Patch {
  uint64_t offset;  // in the pac
  const Replacement* with;
};

// A PACH tree (not BPE-packed nodes: those are left as they are) at file
// offset `base`: the DDS entries the table replaces.
void Walk(const uint8_t* p, size_t n, uint64_t base, const Table& table, std::vector<Patch>& out, int depth = 0) {
  if (depth > 8 || n < 8) return;
  if (!std::memcmp(p, "PACH", 4)) {
    const uint32_t count = Rd32(p + 4);
    const uint64_t body = 8 + uint64_t(count) * 12;
    if (count > 100000 || body > n) return;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t off = Rd32(p + 8 + i * 12 + 4), size = Rd32(p + 8 + i * 12 + 8);
      if (body + off + size > n) continue;
      Walk(p + body + off, size, base + body + off, table, out, depth + 1);
    }
    return;
  }
  if (!std::memcmp(p, "DDS ", 4)) {
    auto it = table.find(Fnv1a64(p, n));
    if (it != table.end() && it->second.size == n) out.push_back({base, &it->second});
  }
}

// The replaced textures in one pac (an EPAC: groups from 0x800, data on
// 0x800-byte sectors from 0x4000).
bool FindPatches(const fs::path& pac, const Table& table, std::vector<Patch>& out) {
  std::ifstream in(pac, std::ios::binary);
  if (!in) return false;
  std::vector<uint8_t> head(0x4000);
  if (!in.read(reinterpret_cast<char*>(head.data()), head.size()) || std::memcmp(head.data(), "EPAC", 4)) return false;
  std::vector<uint8_t> blob;
  for (size_t p = 0x800; p + 12 <= head.size();) {
    if (!Rd32(&head[p])) break;
    const uint32_t count = (Rd32(&head[p + 4]) & 0xFFFF) / 3;
    p += 12;
    for (uint32_t i = 0; i < count && p + 12 <= head.size(); ++i, p += 12) {
      const uint64_t off = 0x4000 + uint64_t(Rd32(&head[p + 4])) * 0x800;
      const uint64_t size = uint64_t(Rd32(&head[p + 8])) * 0x100;
      if (!size || size > (256u << 20)) continue;
      blob.resize(size);
      in.clear();
      in.seekg(std::streamoff(off));
      if (!in.read(reinterpret_cast<char*>(blob.data()), std::streamsize(size))) continue;
      Walk(blob.data(), blob.size(), off, table, out);
    }
  }
  return true;
}

std::string Stamp(const fs::path& src, const std::vector<Patch>& patches) {
  std::error_code ec;
  const auto size = fs::file_size(src, ec);
  const auto time = fs::last_write_time(src, ec).time_since_epoch().count();
  std::string s = std::string(kStampVersion) + " " + std::to_string(static_cast<unsigned long long>(size)) + " " + std::to_string(static_cast<long long>(time));
  for (const Patch& p : patches) s += " " + std::to_string(static_cast<unsigned long long>(p.offset)) + ":" + p.with->file.filename().string();
  return s;
}

// <copy> := <src> with the patches; false if it can't be made.
bool MakeCopy(const fs::path& src, const fs::path& copy, const std::vector<Patch>& patches, const std::string& stamp) {
  std::error_code ec;
  const fs::path stamp_file = copy.string() + ".stamp";
  {
    std::ifstream in(stamp_file);
    std::string have;
    std::getline(in, have);
    if (have == stamp && fs::file_size(copy, ec) == fs::file_size(src, ec) && !ec) return true;
  }
  fs::create_directories(copy.parent_path(), ec);
  fs::remove(stamp_file, ec);
  const fs::path tmp = copy.string() + ".tmp";
  fs::remove(tmp, ec);
  if (!fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec)) {
    REXLOG_WARN("[svr2008] pad icons: could not copy {}: {}", src.string(), ec.message());
    return false;
  }
  {
    std::fstream out(tmp, std::ios::binary | std::ios::in | std::ios::out);
    if (!out) return false;
    std::vector<char> data;
    for (const Patch& p : patches) {
      std::ifstream in(p.with->file, std::ios::binary);
      data.assign(size_t(p.with->size), 0);
      if (!in.read(data.data(), std::streamsize(data.size()))) return false;
      out.seekp(std::streamoff(p.offset));
      out.write(data.data(), std::streamsize(data.size()));
    }
    out.flush();
    if (!out) return false;
  }
  fs::rename(tmp, copy, ec);
  if (ec) return false;
  std::ofstream(stamp_file, std::ios::trunc) << stamp << "\n";
  return true;
}

}  // namespace

void InstallPadIcons(rex::filesystem::VirtualFileSystem* vfs, const fs::path& game, const fs::path& app) {
  std::error_code ec;
  const fs::path dir = app / "pad_icons";
  Table table;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path f = it->path();
    if (f.extension() != ".dds") continue;
    const std::string stem = f.stem().string();
    char* endp = nullptr;
    const uint64_t key = std::strtoull(stem.c_str(), &endp, 16);
    if (stem.size() != 16 || !endp || *endp) continue;
    table[key] = {f, fs::file_size(f, ec)};
  }
  if (table.empty()) {
    REXLOG_INFO("[svr2008] pad icons: none ({} missing) - Xbox buttons", dir.string());
    return;
  }
  const fs::path out_root = app / "UserData" / "pad_icons";
  std::vector<std::string> made;
  for (const char* rel : kPacs) {
    const fs::path src = game / "pac" / rel;
    std::vector<Patch> patches;
    if (!FindPatches(src, table, patches) || patches.empty()) {
      REXLOG_INFO("[svr2008] pad icons: {}: nothing to swap", rel);
      continue;
    }
    const fs::path copy = out_root / "pac" / rel;
    if (MakeCopy(src, copy, patches, Stamp(src, patches))) {
      made.push_back(rel);
      REXLOG_INFO("[svr2008] pad icons: {}: {} textures", rel, patches.size());
    }
  }
  if (made.empty() || !vfs) return;
  // The copies as \PADICONS\pac\..., and the game's paths to them linked there.
  auto device = std::make_unique<rex::filesystem::HostPathDevice>("\\PADICONS", out_root, true);
  if (!device->Initialize() || !vfs->RegisterDevice(std::move(device))) {
    REXLOG_WARN("[svr2008] pad icons: could not mount {}", out_root.string());
    return;
  }
  for (const std::string& rel : made) {
    std::string guest = rel;
    for (char& c : guest)
      if (c == '/') c = '\\';
    vfs->RegisterSymbolicLink("\\Device\\Harddisk0\\Partition1\\pac\\" + guest, "\\PADICONS\\pac\\" + guest);
  }
  REXLOG_INFO("[svr2008] pad icons: PlayStation buttons ({} archives)", made.size());
}

}  // namespace svr2008

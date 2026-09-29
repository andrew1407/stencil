// Host tool for the Linux package: rewrites the ICU common-data package held by a deployed
// libicudata.so.N.M without the trees no ICU call of Qt reads, as a raw .dat that
// packaging/trimicu.cmake links back into a library under ICU's own symbol and soname.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

struct Item {
  std::string name;
  size_t offset = 0;  // from the start of the table of contents
  size_t size = 0;
};

// Qt reads collation, converters, time zones and the locale bundles; it breaks text and names
// languages, regions, currencies and units from its own data.
const char* const kDroppedTrees[] = {"brkitr", "curr", "lang", "rbnf", "region", "translit", "unit"};

bool dropped(const std::string& name) {
  const size_t first = name.find('/');  // past the "icudtNNl/" package prefix
  for (const char* tree : kDroppedTrees) {
    const std::string dir = std::string(tree) + '/';
    if (name.compare(0, dir.size(), dir) == 0) return true;
    if (first != std::string::npos && name.compare(first + 1, dir.size(), dir) == 0) return true;
  }
  return false;
}

size_t le16(const Bytes& b, size_t at) { return size_t(b[at]) | size_t(b[at + 1]) << 8; }

size_t le32(const Bytes& b, size_t at) { return le16(b, at) | le16(b, at + 2) << 16; }

uint64_t le64(const Bytes& b, size_t at) { return uint64_t(le32(b, at)) | uint64_t(le32(b, at + 4)) << 32; }

bool fits(uint64_t at, uint64_t len, size_t total) { return at <= total && len <= total - at; }

void putLe32(Bytes& out, size_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(uint8_t(value >> shift));
}

size_t alignUp(size_t n) { return (n + 15) & ~size_t(15); }  // ICU aligns every item to 16 bytes

bool isDataSymbol(const std::string& name) {
  return name.size() > 9 && name.compare(0, 5, "icudt") == 0 &&
         name.compare(name.size() - 4, 4, "_dat") == 0;
}

// The package is the exported icudtNN_dat object of a 64-bit little-endian ELF library; its
// header must read as ICU common data ("CmnD", little-endian).
bool findPackage(const Bytes& lib, size_t& start, size_t& end) {
  if (lib.size() < 64 || std::memcmp(lib.data(), "\x7f" "ELF", 4) != 0 || lib[4] != 2 || lib[5] != 1)
    return false;
  const uint64_t shoff = le64(lib, 0x28);
  const size_t shentsize = le16(lib, 0x3a), shnum = le16(lib, 0x3c);
  if (shentsize < 64 || !fits(shoff, uint64_t(shnum) * shentsize, lib.size())) return false;
  const auto header = [&](size_t index) { return size_t(shoff) + index * shentsize; };

  for (size_t s = 0; s < shnum; ++s) {
    if (le32(lib, header(s) + 4) != 11) continue;  // SHT_DYNSYM
    const size_t link = le32(lib, header(s) + 40);
    if (link >= shnum) return false;
    const uint64_t strOff = le64(lib, header(link) + 24), strSize = le64(lib, header(link) + 32);
    const uint64_t symOff = le64(lib, header(s) + 24), symSize = le64(lib, header(s) + 32);
    const uint64_t symEnt = le64(lib, header(s) + 56);
    if (symEnt < 24 || !fits(symOff, symSize, lib.size()) || !fits(strOff, strSize, lib.size()))
      return false;

    for (uint64_t sym = symOff; sym + symEnt <= symOff + symSize; sym += symEnt) {
      const size_t nameAt = le32(lib, size_t(sym));
      if (nameAt >= strSize) continue;
      const char* text = reinterpret_cast<const char*>(lib.data() + strOff + nameAt);
      if (!isDataSymbol(std::string(text, strnlen(text, size_t(strSize) - nameAt)))) continue;

      const size_t home = le16(lib, size_t(sym) + 6);
      if (home == 0 || home >= shnum) return false;
      const uint64_t addr = le64(lib, header(home) + 16), fileOff = le64(lib, header(home) + 24);
      const uint64_t value = le64(lib, size_t(sym) + 8), size = le64(lib, size_t(sym) + 16);
      if (value < addr || size < 16 || !fits(fileOff + (value - addr), size, lib.size())) return false;
      start = size_t(fileOff + (value - addr));
      end = start + size_t(size);
      return lib[start + 2] == 0xda && lib[start + 3] == 0x27 && lib[start + 8] == 0 &&
             std::memcmp(&lib[start + 12], "CmnD", 4) == 0;
    }
  }
  return false;
}

// Common data: a header, then at `toc` an item count and one (name, data) offset pair per item,
// both from `toc`; names are NUL-terminated and items sorted by name, each running to the next.
bool readPackage(const Bytes& b, size_t start, size_t end, size_t& toc, std::vector<Item>& items) {
  if (end > b.size() || start > end || end - start < 16) return false;
  toc = start + le16(b, start);
  if (toc > end || end - toc < 4) return false;
  const size_t count = le32(b, toc), span = end - toc;
  if (count == 0 || count > (span - 4) / 8) return false;

  items.clear();
  for (size_t i = 0; i < count; ++i) {
    const size_t nameAt = le32(b, toc + 4 + 8 * i), dataAt = le32(b, toc + 8 + 8 * i);
    if (nameAt >= span || dataAt > span || (!items.empty() && dataAt < items.back().offset))
      return false;
    const char* text = reinterpret_cast<const char*>(&b[toc + nameAt]);
    const size_t length = strnlen(text, span - nameAt);
    if (length == span - nameAt) return false;
    items.push_back({std::string(text, length), dataAt, 0});
  }
  for (size_t i = 0; i < count; ++i)
    items[i].size = (i + 1 < count ? items[i + 1].offset : span) - items[i].offset;
  return true;
}

Bytes writePackage(const Bytes& src, size_t start, size_t toc, const std::vector<Item>& kept) {
  Bytes out(src.begin() + start, src.begin() + toc);
  const size_t header = out.size();
  size_t names = 4 + 8 * kept.size();
  size_t data = names;
  for (const Item& item : kept) data += item.name.size() + 1;

  putLe32(out, kept.size());
  for (const Item& item : kept) {
    data = alignUp(header + data) - header;
    putLe32(out, names);
    putLe32(out, data);
    names += item.name.size() + 1;
    data += item.size;
  }
  for (const Item& item : kept)
    out.insert(out.end(), item.name.c_str(), item.name.c_str() + item.name.size() + 1);
  for (const Item& item : kept) {
    out.resize(alignUp(out.size()), 0);
    const auto from = src.begin() + toc + item.offset;
    out.insert(out.end(), from, from + item.size);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: stencil_trimicu <libicudata.so.N.M> <output.dat>\n");
    return 2;
  }

  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "trimicu: cannot read %s\n", argv[1]);
    return 2;
  }
  const Bytes lib((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t start = 0, end = 0, toc = 0;
  std::vector<Item> items;
  if (!findPackage(lib, start, end) || !readPackage(lib, start, end, toc, items)) {
    std::fprintf(stderr, "trimicu: %s holds no readable ICU data package\n", argv[1]);
    return 2;
  }

  std::vector<Item> kept;
  std::copy_if(items.begin(), items.end(), std::back_inserter(kept),
               [](const Item& item) { return !dropped(item.name); });
  const Bytes out = writePackage(lib, start, toc, kept);

  size_t outToc = 0;
  std::vector<Item> check;
  bool same = readPackage(out, 0, out.size(), outToc, check) && check.size() == kept.size();
  for (size_t i = 0; same && i < kept.size(); ++i) {
    const auto from = lib.begin() + toc + kept[i].offset;
    same = check[i].name == kept[i].name && check[i].size >= kept[i].size &&
           std::equal(from, from + kept[i].size, out.begin() + outToc + check[i].offset);
  }
  if (!same) {
    std::fprintf(stderr, "trimicu: the trimmed package does not read back item for item\n");
    return 3;
  }

  std::ofstream file(argv[2], std::ios::binary);
  file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  if (!file.flush()) {
    std::fprintf(stderr, "trimicu: cannot write %s\n", argv[2]);
    return 3;
  }
  std::printf("trimicu: kept %zu of %zu items, %zu -> %zu bytes\n", kept.size(), items.size(),
              end - start, out.size());
  return 0;
}

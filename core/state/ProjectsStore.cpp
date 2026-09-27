#include "ProjectsStore.hpp"

#include "text.hpp"

#include <algorithm>
#include <cctype>  // std::isdigit in untitledIndex
#include <charconv>
#include <utility>  // std::move

namespace stencil::core {

  namespace {
    // Lowercase base36 of a non-negative value (matches JS Number.toString(36)).
    std::string toBase36(long long v) {
      if (v <= 0) return "0";
      static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
      std::string out;
      while (v > 0) {
        out.push_back(digits[v % 36]);
        v /= 36;
      }
      std::reverse(out.begin(), out.end());
      return out;
    }

    // Parse "Untitled <n>" -> n, or -1 if the name does not match exactly.
    long long untitledIndex(const std::string& name) {
      const std::string prefix = "Untitled ";
      if (name.size() <= prefix.size()) return -1;
      if (name.compare(0, prefix.size(), prefix) != 0) return -1;
      const std::string rest = name.substr(prefix.size());
      for (char c : rest) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
      }
      long long n = -1;  // past LLONG_MAX is result_out_of_range: no match
      const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), n);
      return parsed.ec == std::errc{} ? n : -1;
    }
  }  // namespace

  long long ProjectsStore::periodMs(const std::string& period) {
    return lookup(PERIOD_MS, period, EXPIRY_MS);
  }

  long long ProjectsStore::addPeriod(long long from, const std::string& period) {
    return from + periodMs(period);
  }

  bool ProjectsStore::shouldPersist(const std::optional<std::string>& activeId,
                                    bool temporary) {
    return !temporary && activeId.has_value();
  }

  void ProjectsStore::load(std::vector<ProjectMeta> registry) {
    this->registry = std::move(registry);
    reindex();
  }

  void ProjectsStore::reindex() {
    index.clear();
    index.reserve(registry.size());
    for (std::size_t i = 0; i < registry.size(); ++i)
      index.emplace(registry[i].id, i);  // emplace keeps the FIRST occurrence
  }

  std::vector<const ProjectMeta*> ProjectsStore::listRefs() const {
    std::vector<const ProjectMeta*> out;
    out.reserve(registry.size());
    for (const auto& m : registry) {
      if (!m.id.empty()) out.push_back(&m);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ProjectMeta* a, const ProjectMeta* b) {
                       return a->updatedAt > b->updatedAt;
                     });
    return out;
  }

  std::vector<ProjectMeta> ProjectsStore::list() const {
    const std::vector<const ProjectMeta*> refs = listRefs();
    std::vector<ProjectMeta> out;
    out.reserve(refs.size());
    for (const ProjectMeta* m : refs) out.push_back(*m);
    return out;
  }

  std::vector<ProjectMeta>::iterator ProjectsStore::findById(const std::string& id) {
    const auto at = index.find(id);
    if (at == index.end()) return registry.end();
    return registry.begin() + static_cast<std::ptrdiff_t>(at->second);
  }

  std::vector<ProjectMeta>::const_iterator ProjectsStore::findById(
      const std::string& id) const {
    const auto at = index.find(id);
    if (at == index.end()) return registry.end();
    return registry.begin() + static_cast<std::ptrdiff_t>(at->second);
  }

  const ProjectMeta* ProjectsStore::find(const std::string& id) const {
    const auto it = findById(id);
    return it == registry.end() ? nullptr : &*it;
  }

  void ProjectsStore::remove(const std::string& id) {
    const auto it = findById(id);
    if (it == registry.end()) return;
    registry.erase(it);
    reindex();  // every later position shifted; a duplicate id may have surfaced
  }

  void ProjectsStore::clearAll() {
    registry.clear();
    index.clear();
  }

  bool ProjectsStore::isExpired(const ProjectMeta& meta, long long now) const {
    if (meta.expiresAt == 0) return false;  // keep forever
    return now > meta.expiresAt;
  }

  std::optional<long long> ProjectsStore::expiresAt(const ProjectMeta& meta) const {
    if (meta.expiresAt == 0) return std::nullopt;  // keep forever
    return meta.expiresAt;
  }

  bool ProjectsStore::isExpiringSoon(const ProjectMeta& meta, long long now) const {
    const auto at = expiresAt(meta);
    if (!at.has_value()) return false;
    return *at > now && (*at - now) <= WARN_MS;
  }

  std::vector<std::string> ProjectsStore::sweepExpired(long long now) {
    std::vector<std::string> removed;
    const auto expired = [&](const ProjectMeta& m) { return isExpired(m, now); };
    for (const ProjectMeta& m : registry)
      if (expired(m)) removed.push_back(m.id);
    if (removed.empty()) return removed;
    // One erase pass and one reindex; remove(id) per row reindexed the whole registry each time.
    registry.erase(std::remove_if(registry.begin(), registry.end(), expired), registry.end());
    reindex();
    return removed;
  }

  std::string ProjectsStore::createId(long long now,
                                      const std::string& salt) const {
    return "p_" + toBase36(now) + "_" + salt;
  }

  std::string ProjectsStore::defaultName() const {
    long long max = 0;
    for (const auto& m : registry) {
      max = std::max(max, untitledIndex(m.name));
    }
    return "Untitled " + std::to_string(max + 1);
  }

  bool ProjectsStore::nameExists(const std::string& name,
                                 const std::string& exceptId) const {
    const std::string n = trimLowerAscii(name);
    if (n.empty()) return false;
    for (const auto& m : registry) {
      if (m.id.empty() || m.id == exceptId) continue;
      if (trimLowerAscii(m.name) == n) return true;
    }
    return false;
  }

  ProjectsStore::NameCheck ProjectsStore::validateName(
      const std::string& name, const std::string& exceptId) const {
    const std::string clean = std::string(trimAscii(name));
    if (clean.empty()) return {false, "Name can't be empty"};
    if (clean.size() > MAX_NAME_LENGTH)
      return {false, "Name is too long (max 80 characters)"};
    if (nameExists(clean, exceptId)) return {false, "\"" + clean + "\" is already taken"};
    return {true, ""};
  }

}

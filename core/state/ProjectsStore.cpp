#include "ProjectsStore.hpp"

#include "text.hpp"

#include <algorithm>
#include <cctype>  // std::isdigit in untitledIndex
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
    int untitledIndex(const std::string& name) {
      const std::string prefix = "Untitled ";
      if (name.size() <= prefix.size()) return -1;
      if (name.compare(0, prefix.size(), prefix) != 0) return -1;
      const std::string rest = name.substr(prefix.size());
      for (char c : rest) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
      }
      try {
        return std::stoi(rest);
      } catch (...) {
        return -1;
      }
    }
  }  // namespace

  long long ProjectsStore::periodMs(const std::string& period) {
    // Fixed-duration presets (must match browser projectsStore.js PERIOD_MS).
    if (period == "day") return DAY_MS;
    if (period == "fortnight") return 14 * DAY_MS;
    if (period == "month") return 30 * DAY_MS;
    if (period == "3month") return 90 * DAY_MS;
    if (period == "6month") return 180 * DAY_MS;
    if (period == "year") return 365 * DAY_MS;
    return EXPIRY_MS;
  }

  long long ProjectsStore::addPeriod(long long from, const std::string& period) {
    return from + periodMs(period);
  }

  bool ProjectsStore::shouldPersist(const std::optional<std::string>& activeId,
                                    bool temporary) {
    return !temporary && activeId.has_value();
  }

  void ProjectsStore::load(std::vector<ProjectMeta> registry) {
    registry_ = std::move(registry);
    reindex();
  }

  void ProjectsStore::reindex() {
    index_.clear();
    index_.reserve(registry_.size());
    for (std::size_t i = 0; i < registry_.size(); ++i)
      index_.emplace(registry_[i].id, i);  // emplace keeps the FIRST occurrence
  }

  std::vector<const ProjectMeta*> ProjectsStore::listRefs() const {
    std::vector<const ProjectMeta*> out;
    out.reserve(registry_.size());
    for (const auto& m : registry_) {
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
    const auto at = index_.find(id);
    if (at == index_.end()) return registry_.end();
    return registry_.begin() + static_cast<std::ptrdiff_t>(at->second);
  }

  std::vector<ProjectMeta>::const_iterator ProjectsStore::findById(
      const std::string& id) const {
    const auto at = index_.find(id);
    if (at == index_.end()) return registry_.end();
    return registry_.begin() + static_cast<std::ptrdiff_t>(at->second);
  }

  const ProjectMeta* ProjectsStore::find(const std::string& id) const {
    const auto it = findById(id);
    return it == registry_.end() ? nullptr : &*it;
  }

  std::optional<ProjectMeta> ProjectsStore::getMeta(const std::string& id) const {
    const ProjectMeta* m = find(id);
    if (m == nullptr) return std::nullopt;
    return *m;
  }

  ProjectMeta ProjectsStore::upsert(ProjectMeta meta, long long now) {
    return upsertMoved(std::move(meta), now);
  }

  const ProjectMeta& ProjectsStore::upsertMoved(ProjectMeta&& meta, long long now) {
    meta.updatedAt = now;
    if (meta.createdAt == 0) meta.createdAt = now;
    const auto it = findById(meta.id);
    if (it != registry_.end()) {
      *it = std::move(meta);
      return *it;
    }
    index_.emplace(meta.id, registry_.size());  // before the move empties meta.id
    registry_.push_back(std::move(meta));
    return registry_.back();
  }

  bool ProjectsStore::touch(const std::string& id, long long now) {
    const auto it = findById(id);
    if (it == registry_.end()) return false;
    it->updatedAt = now;
    return true;
  }

  bool ProjectsStore::setExpiration(const std::string& id, long long expiresAt,
                                    const std::string& refreshPeriod,
                                    bool autoRefresh) {
    const auto it = findById(id);
    if (it == registry_.end()) return false;
    it->expiresAt = expiresAt;
    it->refreshPeriod = refreshPeriod.empty() ? DEFAULT_PERIOD : refreshPeriod;
    it->autoRefresh = autoRefresh;
    return true;
  }

  void ProjectsStore::remove(const std::string& id) {
    const auto it = findById(id);
    if (it == registry_.end()) return;
    registry_.erase(it);
    reindex();  // every later position shifted; a duplicate id may have surfaced
  }

  void ProjectsStore::clearAll() {
    registry_.clear();
    index_.clear();
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
    for (const auto& m : registry_) {
      if (isExpired(m, now)) removed.push_back(m.id);
    }
    for (const auto& id : removed) remove(id);
    return removed;
  }

  std::string ProjectsStore::createId(long long now,
                                      const std::string& salt) const {
    return "p_" + toBase36(now) + "_" + salt;
  }

  std::string ProjectsStore::defaultName() const {
    int max = 0;
    for (const auto& m : registry_) {
      max = std::max(max, untitledIndex(m.name));
    }
    return "Untitled " + std::to_string(max + 1);
  }

  bool ProjectsStore::nameExists(const std::string& name,
                                 const std::string& exceptId) const {
    const std::string n = trimLowerAscii(name);
    if (n.empty()) return false;
    for (const auto& m : registry_) {
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

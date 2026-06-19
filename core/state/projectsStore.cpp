#include "projectsStore.hpp"

#include "text.hpp"

#include <algorithm>
#include <cctype>  // std::isdigit in untitledIndex

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

  bool ProjectsStore::shouldPersist(const std::optional<std::string>& activeId,
                                    bool temporary) {
    return !temporary && activeId.has_value();
  }

  void ProjectsStore::load(std::vector<ProjectMeta> registry) {
    registry_ = std::move(registry);
  }

  std::vector<ProjectMeta> ProjectsStore::list() const {
    std::vector<ProjectMeta> out;
    for (const auto& m : registry_) {
      if (!m.id.empty()) out.push_back(m);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ProjectMeta& a, const ProjectMeta& b) {
                       return a.updatedAt > b.updatedAt;
                     });
    return out;
  }

  std::optional<ProjectMeta> ProjectsStore::getMeta(const std::string& id) const {
    for (const auto& m : registry_) {
      if (m.id == id) return m;
    }
    return std::nullopt;
  }

  ProjectMeta ProjectsStore::upsert(ProjectMeta meta, long long now) {
    meta.updatedAt = now;
    if (meta.createdAt == 0) meta.createdAt = now;
    const auto it = std::find_if(registry_.begin(), registry_.end(),
                                 [&](const ProjectMeta& m) {
                                   return m.id == meta.id;
                                 });
    if (it == registry_.end()) registry_.push_back(meta);
    else *it = meta;
    return meta;
  }

  bool ProjectsStore::touch(const std::string& id, long long now) {
    for (auto& m : registry_) {
      if (m.id == id) {
        m.updatedAt = now;
        return true;
      }
    }
    return false;
  }

  void ProjectsStore::remove(const std::string& id) {
    registry_.erase(std::remove_if(registry_.begin(), registry_.end(),
                                   [&](const ProjectMeta& m) {
                                     return m.id == id;
                                   }),
                    registry_.end());
  }

  void ProjectsStore::clearAll() {
    registry_.clear();
  }

  bool ProjectsStore::isExpired(const ProjectMeta& meta, long long now) const {
    if (meta.updatedAt == 0) return false;
    return (now - meta.updatedAt) > EXPIRY_MS;
  }

  std::optional<long long> ProjectsStore::expiresAt(const ProjectMeta& meta) const {
    if (meta.updatedAt == 0) return std::nullopt;
    return meta.updatedAt + EXPIRY_MS;
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
    if (clean.size() > 80) return {false, "Name is too long (max 80 characters)"};
    if (nameExists(clean, exceptId)) return {false, "\"" + clean + "\" is already taken"};
    return {true, ""};
  }

}

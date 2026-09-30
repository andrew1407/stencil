#include "doctest.h"
#include "ProjectsStore.hpp"

using namespace stencil::core;

// Mirrors the pure-logic parts of browser/tests/core/project/store/projectsStore.test.js.

static ProjectMeta mk(const std::string& id, const std::string& name,
                      long long updatedAt) {
  ProjectMeta m;
  m.id = id;
  m.name = name;
  m.createdAt = updatedAt;
  m.updatedAt = updatedAt;
  return m;
}

// Like mk, but also seeds an explicit expiresAt for the stored-expiry tests.
static ProjectMeta mkE(const std::string& id, const std::string& name,
                       long long updatedAt, long long expiresAt) {
  ProjectMeta m = mk(id, name, updatedAt);
  m.expiresAt = expiresAt;
  return m;
}

TEST_CASE("shouldPersist only with an active, non-temporary project") {
  CHECK(ProjectsStore::shouldPersist(std::string("p_1"), false));
  CHECK_FALSE(ProjectsStore::shouldPersist(std::string("p_1"), true));
  CHECK_FALSE(ProjectsStore::shouldPersist(std::nullopt, false));
}

TEST_CASE("load indexes the registry; list is updatedAt-desc without id-less rows") {
  ProjectsStore s;
  s.load({mk("a", "Alpha", 100), mk("b", "Bravo", 300), mk("", "Orphan", 400), mk("c", "Charlie", 200)});
  const auto list = s.list();
  REQUIRE(list.size() == 3);
  CHECK(list[0].id == "b");  // most recently updated first
  CHECK(list[1].id == "c");
  CHECK(list[2].id == "a");
  const auto refs = s.listRefs();
  REQUIRE(refs.size() == 3);
  CHECK(refs[0]->id == "b");
  CHECK(s.getRegistry().size() == 4);  // insertion order, the id-less row kept
  CHECK(s.getRegistry()[2].name == "Orphan");
  REQUIRE(s.find("c") != nullptr);
  CHECK(s.find("c")->name == "Charlie");
  CHECK(s.find("nope") == nullptr);
}

TEST_CASE("a duplicate id finds its first row; remove surfaces the next") {
  ProjectsStore s;
  s.load({mk("a", "First", 100), mk("b", "Bravo", 200), mk("a", "Second", 300)});
  REQUIRE(s.find("a") != nullptr);
  CHECK(s.find("a")->name == "First");
  s.remove("a");
  REQUIRE(s.find("a") != nullptr);
  CHECK(s.find("a")->name == "Second");
  CHECK(s.find("b")->name == "Bravo");  // positions shifted, the index followed
  s.remove("a");
  s.remove("nope");  // unknown id: nothing happens
  CHECK(s.find("a") == nullptr);
  REQUIRE(s.list().size() == 1);
  CHECK(s.list()[0].id == "b");
}

TEST_CASE("periodMs / addPeriod presets (fixed durations)") {
  constexpr long long DAY = 24LL * 60 * 60 * 1000;
  CHECK(ProjectsStore::periodMs("day") == DAY);
  CHECK(ProjectsStore::periodMs("week") == 7 * DAY);
  CHECK(ProjectsStore::periodMs("week") == ProjectsStore::EXPIRY_MS);
  CHECK(ProjectsStore::periodMs("fortnight") == 14 * DAY);
  CHECK(ProjectsStore::periodMs("month") == 30 * DAY);
  CHECK(ProjectsStore::periodMs("3month") == 90 * DAY);
  CHECK(ProjectsStore::periodMs("6month") == 180 * DAY);
  CHECK(ProjectsStore::periodMs("year") == 365 * DAY);
  // Unknown / empty falls back to one week.
  CHECK(ProjectsStore::periodMs("") == 7 * DAY);
  CHECK(ProjectsStore::periodMs("decade") == 7 * DAY);
  CHECK(ProjectsStore::addPeriod(1000, "day") == 1000 + DAY);
}

TEST_CASE("expiry keyed on stored expiresAt; 0 == keep forever") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  ProjectMeta fresh = mkE("fresh", "F", now, now + 1000);
  ProjectMeta old = mkE("old", "O", now, now - 1);
  ProjectMeta keep = mkE("keep", "K", now, 0);  // never expires
  CHECK_FALSE(s.isExpired(fresh, now));
  CHECK(s.isExpired(old, now));
  CHECK_FALSE(s.isExpired(keep, now));  // keep forever
  CHECK(s.expiresAt(fresh).value() == now + 1000);
  CHECK(s.expiresAt(keep) == std::nullopt);  // keep forever → no date
}

TEST_CASE("isExpiringSoon: within a day of expiry, but not once expired") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  // Expires in half a day → inside the warning window.
  ProjectMeta soon = mkE("soon", "S", now, now + ProjectsStore::WARN_MS / 2);
  CHECK(s.isExpiringSoon(soon, now));
  // Expires in two days → outside the window.
  ProjectMeta later = mkE("later", "L", now, now + 2 * ProjectsStore::WARN_MS);
  CHECK_FALSE(s.isExpiringSoon(later, now));
  // Already expired → false (expired takes precedence in the UI).
  ProjectMeta old = mkE("old", "O", now, now - 1);
  CHECK(s.isExpired(old, now));
  CHECK_FALSE(s.isExpiringSoon(old, now));
  // Keep forever → never "expiring soon".
  ProjectMeta keep = mkE("keep", "K", now, 0);
  CHECK_FALSE(s.isExpiringSoon(keep, now));
}

TEST_CASE("isExpiringSoon boundary: inclusive at exactly WARN_MS remaining") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  ProjectMeta edge = mkE("edge", "E", now, now + ProjectsStore::WARN_MS);
  CHECK(s.isExpiringSoon(edge, now));
  ProjectMeta justOut = mkE("out", "O", now, now + ProjectsStore::WARN_MS + 1);
  CHECK_FALSE(s.isExpiringSoon(justOut, now));
}

TEST_CASE("sweepExpired removes only expired and returns their ids") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  s.load({mkE("fresh", "F", now, now + ProjectsStore::EXPIRY_MS), mkE("old", "O", now, now - 1),
          mkE("keep", "K", now, 0)});  // keep forever, must survive
  const auto removed = s.sweepExpired(now);
  REQUIRE(removed.size() == 1);
  CHECK(removed[0] == "old");
  CHECK(s.list().size() == 2);
  CHECK(s.find("old") == nullptr);
  CHECK(s.find("keep") != nullptr);
}

TEST_CASE("defaultName is one past the highest Untitled index") {
  ProjectsStore s;
  CHECK(s.defaultName() == "Untitled 1");
  std::vector<ProjectMeta> reg = {mk("a", "Untitled 1", 1), mk("b", "Untitled 3", 2), mk("c", "My Drawing", 3)};
  s.load(reg);
  CHECK(s.defaultName() == "Untitled 4");
  // Past INT_MAX the index still counts, as projectNaming.js's parseInt does.
  reg.push_back(mk("d", "Untitled 99999999999", 4));
  s.load(reg);
  CHECK(s.defaultName() == "Untitled 100000000000");
}

TEST_CASE("nameExists: case-insensitive, trims, excludes a given id") {
  ProjectsStore s;
  s.load({mk("a", "Floor Plan", 1), mk("b", "Roof", 2)});
  CHECK(s.nameExists("floor plan"));        // case-insensitive
  CHECK(s.nameExists("  Roof  "));          // trims
  CHECK_FALSE(s.nameExists("Basement"));
  CHECK_FALSE(s.nameExists("Floor Plan", "a"));   // its own name doesn't collide
  CHECK(s.nameExists("Floor Plan", "b"));
  CHECK_FALSE(s.nameExists(""));
}

TEST_CASE("validateName: ok + reason for empty / too-long / duplicate") {
  ProjectsStore s;
  s.load({mk("a", "Roof", 1)});
  CHECK(s.validateName("Floor").ok);
  CHECK_FALSE(s.validateName("   ").ok);
  CHECK(s.validateName("").reason.find("empty") != std::string::npos);
  CHECK_FALSE(s.validateName(std::string(81, 'x')).ok);
  CHECK_FALSE(s.validateName("roof").ok);                 // case-insensitive duplicate
  CHECK(s.validateName("roof").reason.find("taken") != std::string::npos);
  CHECK(s.validateName("Roof", "a").ok);                  // its own name is fine
}

TEST_CASE("createId has the expected shape") {
  ProjectsStore s;
  const std::string id = s.createId(0, "abc123");
  CHECK(id.rfind("p_", 0) == 0);
  CHECK(id.find("_abc123") != std::string::npos);
}

TEST_CASE("clearAll empties the registry") {
  ProjectsStore s;
  s.load({mk("a", "A", 1)});
  s.clearAll();
  CHECK(s.list().empty());
  CHECK(s.find("a") == nullptr);
}

// Mirrors browser/tests/core/project/meta/projectCopyName.test.js case for case.
static ProjectsStore named(std::initializer_list<const char*> names) {
  std::vector<ProjectMeta> reg;
  long long i = 0;
  for (const char* n : names) { reg.push_back(mk("p" + std::to_string(i), n, i)); ++i; }
  ProjectsStore s;
  s.load(reg);
  return s;
}

TEST_CASE("copySuffixName: -copy, then the lowest free -copy(N)") {
  CHECK(named({"photo"}).copySuffixName("photo") == "photo-copy");
  CHECK(named({"photo", "photo-copy"}).copySuffixName("photo") == "photo-copy(1)");
  CHECK(named({"photo-copy", "photo-copy(1)", "photo-copy(3)"}).copySuffixName("photo") == "photo-copy(2)");
  CHECK(named({"photo", "photo-copy"}).copySuffixName("photo-copy") == "photo-copy(1)");
  CHECK(named({"photo-copy", "photo-copy(1)", "photo-copy(2)"}).copySuffixName("photo-copy(2)") == "photo-copy(3)");
  CHECK(named({"  PHOTO-COPY "}).copySuffixName("photo") == "photo-copy(1)");
  CHECK(named({}).copySuffixName("  photo  ") == "photo-copy");
  CHECK(named({}).copySuffixName("   ") == "Untitled-copy");
  CHECK(named({}).copySuffixName("-copy(4)") == "Untitled-copy");
  CHECK(named({}).copySuffixName("a-copy b") == "a-copy b-copy");
}

TEST_CASE("copySuffixName: a long base is cut so the whole name fits MAX_NAME_LENGTH") {
  const std::string first = named({}).copySuffixName(std::string(100, 'x'));
  CHECK(first == std::string(75, 'x') + "-copy");
  CHECK(first.size() == ProjectsStore::MAX_NAME_LENGTH);
  const std::string second = named({first.c_str()}).copySuffixName(std::string(100, 'x'));
  CHECK(second == std::string(72, 'x') + "-copy(1)");
  // A two-byte character straddling the cut is dropped whole, never split.
  const std::string wide = std::string(74, 'x') + "\xC3\xA9" + "tail";
  CHECK(named({}).copySuffixName(wide) == std::string(74, 'x') + "-copy");
}

#include "doctest.h"
#include "projectsStore.hpp"

using namespace stencil::core;

// Mirrors the pure-logic parts of browser/tests/projectsStore.test.js.

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

TEST_CASE("upsert inserts then replaces; list is updatedAt-desc") {
  ProjectsStore s;
  s.upsert(mk("a", "Alpha", 0), 100);
  s.upsert(mk("b", "Bravo", 0), 200);
  s.upsert(mk("a", "Alpha2", 0), 300);  // replace a, newest
  const auto list = s.list();
  REQUIRE(list.size() == 2);
  CHECK(list[0].id == "a");        // most recently updated first
  CHECK(list[0].name == "Alpha2");
  CHECK(list[1].id == "b");
}

TEST_CASE("upsert stamps updatedAt and preserves createdAt") {
  ProjectsStore s;
  ProjectMeta m = s.upsert(mk("a", "Alpha", 0), 100);
  CHECK(m.createdAt == 100);
  CHECK(m.updatedAt == 100);
  m = s.upsert(m, 500);
  CHECK(m.createdAt == 100);  // unchanged
  CHECK(m.updatedAt == 500);
}

TEST_CASE("touch bumps updatedAt; returns false for missing id") {
  ProjectsStore s;
  s.upsert(mk("a", "Alpha", 0), 100);
  CHECK(s.touch("a", 999));
  CHECK(s.getMeta("a")->updatedAt == 999);
  CHECK_FALSE(s.touch("nope", 999));
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

TEST_CASE("setExpiration sets fields exactly, no updatedAt bump") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  s.upsert(mkE("a", "A", now, now + 1000), now);  // nearly due
  // Renew via addPeriod, as the Refresh button / open-time snap do.
  CHECK(s.setExpiration("a", ProjectsStore::addPeriod(now, "month"), "month", true));
  const auto m = *s.getMeta("a");
  CHECK(m.expiresAt == now + ProjectsStore::periodMs("month"));
  CHECK(m.refreshPeriod == "month");
  CHECK(m.autoRefresh);
  CHECK(m.updatedAt == now);  // setExpiration must NOT bump updatedAt
  CHECK_FALSE(s.isExpired(m, now));
  // Empty period normalises to the default.
  CHECK(s.setExpiration("a", 0, "", false));
  CHECK(s.getMeta("a")->refreshPeriod == ProjectsStore::DEFAULT_PERIOD);
  CHECK(s.getMeta("a")->expiresAt == 0);  // keep forever
  CHECK_FALSE(s.setExpiration("missing", 0, "week", true));
}

TEST_CASE("sweepExpired removes only expired and returns their ids") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  s.upsert(mkE("fresh", "F", now, now + ProjectsStore::EXPIRY_MS), now);
  s.upsert(mkE("old", "O", now, now - 1), now);
  s.upsert(mkE("keep", "K", now, 0), now);  // keep forever, must survive
  const auto removed = s.sweepExpired(now);
  REQUIRE(removed.size() == 1);
  CHECK(removed[0] == "old");
  CHECK(s.list().size() == 2);
  CHECK(s.getMeta("old") == std::nullopt);
  CHECK(s.getMeta("keep") != std::nullopt);
}

TEST_CASE("defaultName is one past the highest Untitled index") {
  ProjectsStore s;
  CHECK(s.defaultName() == "Untitled 1");
  s.upsert(mk("a", "Untitled 1", 0), 1);
  s.upsert(mk("b", "Untitled 3", 0), 2);
  s.upsert(mk("c", "My Drawing", 0), 3);
  CHECK(s.defaultName() == "Untitled 4");
}

TEST_CASE("nameExists: case-insensitive, trims, excludes a given id") {
  ProjectsStore s;
  s.upsert(mk("a", "Floor Plan", 0), 1);
  s.upsert(mk("b", "Roof", 0), 2);
  CHECK(s.nameExists("floor plan"));        // case-insensitive
  CHECK(s.nameExists("  Roof  "));          // trims
  CHECK_FALSE(s.nameExists("Basement"));
  CHECK_FALSE(s.nameExists("Floor Plan", "a"));   // its own name doesn't collide
  CHECK(s.nameExists("Floor Plan", "b"));
  CHECK_FALSE(s.nameExists(""));
}

TEST_CASE("validateName: ok + reason for empty / too-long / duplicate") {
  ProjectsStore s;
  s.upsert(mk("a", "Roof", 0), 1);
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
  s.upsert(mk("a", "A", 0), 1);
  s.clearAll();
  CHECK(s.list().empty());
}

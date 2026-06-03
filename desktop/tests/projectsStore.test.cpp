#include "doctest.h"
#include "core/projectsStore.hpp"

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

TEST_CASE("expiry: one week boundary") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  ProjectMeta fresh = mk("fresh", "F", now - 1000);
  ProjectMeta old = mk("old", "O", now - ProjectsStore::EXPIRY_MS - 1);
  CHECK_FALSE(s.isExpired(fresh, now));
  CHECK(s.isExpired(old, now));
  CHECK(s.expiresAt(fresh).value() == fresh.updatedAt + ProjectsStore::EXPIRY_MS);
}

TEST_CASE("isExpiringSoon: within a day of expiry, but not once expired") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  // Expires in half a day → inside the warning window.
  ProjectMeta soon =
      mk("soon", "S", now - ProjectsStore::EXPIRY_MS + ProjectsStore::WARN_MS / 2);
  CHECK(s.isExpiringSoon(soon, now));
  // Expires in two days → outside the window.
  ProjectMeta later =
      mk("later", "L", now - ProjectsStore::EXPIRY_MS + 2 * ProjectsStore::WARN_MS);
  CHECK_FALSE(s.isExpiringSoon(later, now));
  // Already expired → false (expired takes precedence in the UI).
  ProjectMeta old = mk("old", "O", now - ProjectsStore::EXPIRY_MS - 1);
  CHECK(s.isExpired(old, now));
  CHECK_FALSE(s.isExpiringSoon(old, now));
}

TEST_CASE("isExpiringSoon boundary: inclusive at exactly WARN_MS remaining") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  ProjectMeta edge =
      mk("edge", "E", now - ProjectsStore::EXPIRY_MS + ProjectsStore::WARN_MS);
  CHECK(s.isExpiringSoon(edge, now));
  ProjectMeta justOut =
      mk("out", "O", now - ProjectsStore::EXPIRY_MS + ProjectsStore::WARN_MS + 1);
  CHECK_FALSE(s.isExpiringSoon(justOut, now));
}

TEST_CASE("touch renews the expiry window from now") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  s.upsert(mk("a", "A", 0), now - ProjectsStore::EXPIRY_MS + 1000);  // nearly due
  CHECK(s.isExpiringSoon(*s.getMeta("a"), now));
  CHECK(s.touch("a", now));  // renew
  CHECK_FALSE(s.isExpiringSoon(*s.getMeta("a"), now));
  CHECK_FALSE(s.isExpired(*s.getMeta("a"), now));
  CHECK(s.expiresAt(*s.getMeta("a")).value() == now + ProjectsStore::EXPIRY_MS);
}

TEST_CASE("sweepExpired removes only expired and returns their ids") {
  ProjectsStore s;
  const long long now = 10LL * ProjectsStore::EXPIRY_MS;
  s.upsert(mk("fresh", "F", 0), now);
  s.upsert(mk("old", "O", 0), now - ProjectsStore::EXPIRY_MS - 1);
  const auto removed = s.sweepExpired(now);
  REQUIRE(removed.size() == 1);
  CHECK(removed[0] == "old");
  CHECK(s.list().size() == 1);
  CHECK(s.getMeta("old") == std::nullopt);
}

TEST_CASE("defaultName is one past the highest Untitled index") {
  ProjectsStore s;
  CHECK(s.defaultName() == "Untitled 1");
  s.upsert(mk("a", "Untitled 1", 0), 1);
  s.upsert(mk("b", "Untitled 3", 0), 2);
  s.upsert(mk("c", "My Drawing", 0), 3);
  CHECK(s.defaultName() == "Untitled 4");
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

// Shared harness for the headless desktop tests: the failure counter and the
// common check(ok, msg) reporter. Per-file helpers (near, pumpFor) stay local.
#pragma once

#include <cstdio>

inline int failures = 0;
inline void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

// The op-plan ABI, identical on both extern "C" surfaces. Included by wasmOpplanApi.cpp and
// cliApi.cpp INSIDE their extern "C" block, each having defined STENCIL_ABI(wasmName, cliName)
// first. Editing this file changes BOTH ABIs.
#ifndef STENCIL_ABI
#  error "define STENCIL_ABI(wasmName, cliName) before including abi/opplanShared.inc"
#endif

/* Handle-owned strings: a const char* here stays valid until its handle is destroyed. The tables
 * are guarded by one mutex (ctypes releases the GIL). An unknown handle: NULL / 0 / -1. */

static std::mutex& opplanLock() {
  static std::mutex lock;
  return lock;
}

static stencil::core::abi::HandleTable<stencil::core::opplan::Schema>& opplanSchemas() {
  static stencil::core::abi::HandleTable<stencil::core::opplan::Schema> table;
  return table;
}

static stencil::core::abi::HandleTable<stencil::core::opplan::Result>& opplanResults() {
  static stencil::core::abi::HandleTable<stencil::core::opplan::Result> table;
  return table;
}

// `len` bytes of opRegistry.json resolved for `surface`; `capabilities` NULL = all wired, else a
// comma-separated list gating each op's `requires`. A handle even for a bad registry — read
// opplanSchemaError; 0 only when a pointer is null.
int STENCIL_ABI(opplanSchemaCreate, opplanSchemaCreate)(const char* registryJson, int len, const char* surface,
                                                       const char* capabilities) {
  if (!registryJson || len < 0 || !surface) return 0;
  std::lock_guard<std::mutex> hold(opplanLock());
  const int h = opplanSchemas().create();
  opplanSchemas().get(h)->load(std::string_view(registryJson, static_cast<std::size_t>(len)), surface,
                               capabilities);
  return h;
}

// "" when the schema loaded; else why not (and opplanParse refuses it).
const char* STENCIL_ABI(opplanSchemaError, opplanSchemaError)(int s) {
  std::lock_guard<std::mutex> hold(opplanLock());
  const auto* schema = opplanSchemas().get(s);
  return schema ? schema->error.c_str() : nullptr;
}

// The resolved surface as JSON: entries, forbidden names, limits, registry bytes and FNV-1a.
const char* STENCIL_ABI(opplanSchemaEntries, opplanSchemaEntries)(int s) {
  std::lock_guard<std::mutex> hold(opplanLock());
  const auto* schema = opplanSchemas().get(s);
  return schema && schema->error.empty() ? schema->entriesJson().c_str() : nullptr;
}

void STENCIL_ABI(opplanSchemaDestroy, opplanSchemaDestroy)(int s) {
  std::lock_guard<std::mutex> hold(opplanLock());
  opplanSchemas().destroy(s);
}

// A model reply's `len` bytes → a result handle, or 0 for an unknown or broken schema.
int STENCIL_ABI(opplanParse, opplanParse)(int s, const char* text, int len) {
  if (!text || len < 0) return 0;
  std::lock_guard<std::mutex> hold(opplanLock());
  const auto* schema = opplanSchemas().get(s);
  if (!schema || !schema->error.empty()) return 0;
  const int p = opplanResults().create();
  *opplanResults().get(p) =
      stencil::core::opplan::walkPlan(*schema, std::string_view(text, static_cast<std::size_t>(len)));
  return p;
}

// 0 valid, 1 chat-only, 2 invalid.
int STENCIL_ABI(opplanStatus, opplanStatus)(int p) {
  std::lock_guard<std::mutex> hold(opplanLock());
  const auto* r = opplanResults().get(p);
  return r ? static_cast<int>(r->status) : -1;
}

// {status, reply, actions, variants, ask, warnings, error}, byte-equal to the JS twin's JSON.
const char* STENCIL_ABI(opplanJson, opplanJson)(int p) {
  std::lock_guard<std::mutex> hold(opplanLock());
  const auto* r = opplanResults().get(p);
  return r ? r->text.c_str() : nullptr;
}

void STENCIL_ABI(opplanDestroy, opplanDestroy)(int p) {
  std::lock_guard<std::mutex> hold(opplanLock());
  opplanResults().destroy(p);
}

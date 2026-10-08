// The .stc script ABI, identical on both extern "C" surfaces. Included by wasmScriptApi.cpp
// and cliApi.cpp INSIDE their extern "C" block, each having defined
// STENCIL_ABI(wasmName, cliName) first. Editing this file changes BOTH ABIs.
#ifndef STENCIL_ABI
#  error "define STENCIL_ABI(wasmName, cliName) before including abi/scriptShared.inc"
#endif

/* Handle-owned strings: every const char* here stays valid until scriptDestroy(h) — an
 * exception to the C ABI's static returned strings. An unknown handle: NULL / 0 / -1. */

// One mutex over the table and the dump memo (ctypes releases the GIL); static, not an
// anonymous namespace, because this file is included inside an extern "C" block.
static std::mutex& scriptLock() {
  static std::mutex lock;
  return lock;
}

static stencil::core::abi::HandleTable<stencil::core::script::ScriptProgram>& scriptTable() {
  static stencil::core::abi::HandleTable<stencil::core::script::ScriptProgram> table;
  return table;
}

static const stencil::core::script::Op* scriptOpAtIndex(int h, int i) {
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->getOps().size())) return nullptr;
  return &p->getOps()[static_cast<std::size_t>(i)];
}

// Parses `len` bytes and returns a handle, or 0 when the text pointer is null.
int STENCIL_ABI(scriptParse, scriptParse)(const char* text, int len) {
  if (!text || len < 0) return 0;
  auto program = stencil::core::script::ScriptProgram::parse(text, len);
  std::lock_guard<std::mutex> hold(scriptLock());
  const int h = scriptTable().create();
  *scriptTable().get(h) = std::move(program);
  return h;
}

void STENCIL_ABI(scriptDestroy, scriptDestroy)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  scriptTable().destroy(h);
}

int STENCIL_ABI(scriptErrorCount, scriptErrorCount)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  return p ? p->errorCount() : -1;
}

int STENCIL_ABI(scriptDiagCount, scriptDiagCount)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->getDiagnostics().size()) : -1;
}

// Returns the message; *sev is 0 error / 1 warning, and *code the stable code string.
const char* STENCIL_ABI(scriptDiagAt, scriptDiagAt)(int h, int i, int* sev, int* line,
                                                    int* col, int* len, const char** code) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->getDiagnostics().size())) return nullptr;
  const stencil::core::script::Diagnostic& d = p->getDiagnostics()[static_cast<std::size_t>(i)];
  if (sev) *sev = static_cast<int>(d.severity);
  if (line) *line = d.line;
  if (col) *col = d.col;
  if (len) *len = d.len;
  if (code) *code = d.code.c_str();
  return d.message.c_str();
}

// The span a diagnostic also points at: the `@use stencil` call that expanded the template it
// came from. 1 with line, col and len written, 0 when it has none, -1 on an unknown handle/index.
int STENCIL_ABI(scriptDiagRelated, scriptDiagRelated)(int h, int i, int* line, int* col,
                                                      int* len) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->getDiagnostics().size())) return -1;
  const stencil::core::script::Diagnostic& d = p->getDiagnostics()[static_cast<std::size_t>(i)];
  if (d.relatedLine <= 0) return 0;
  if (line) *line = d.relatedLine;
  if (col) *col = d.relatedCol;
  if (len) *len = d.relatedLen;
  return 1;
}

int STENCIL_ABI(scriptTokenCount, scriptTokenCount)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->getTokens().size()) : -1;
}

int STENCIL_ABI(scriptTokenAt, scriptTokenAt)(int h, int i, int* kind, int* line, int* col,
                                              int* len) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->getTokens().size())) return 0;
  const stencil::core::script::Token& t = p->getTokens()[static_cast<std::size_t>(i)];
  if (kind) *kind = static_cast<int>(t.kind);
  if (line) *line = t.line;
  if (col) *col = t.col;
  if (len) *len = t.len;
  return 1;
}

int STENCIL_ABI(scriptBlockCount, scriptBlockCount)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->getBlocks().size()) : -1;
}

// Returns the source spec ("" for the implicit project block).
const char* STENCIL_ABI(scriptBlockAt, scriptBlockAt)(int h, int i, int* kind, int* frame,
                                                      int* opStart, int* opCount) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->getBlocks().size())) return nullptr;
  const stencil::core::script::Block& b = p->getBlocks()[static_cast<std::size_t>(i)];
  if (kind) *kind = static_cast<int>(b.kind);
  if (frame) *frame = b.frame;
  if (opStart) *opStart = b.opStart;
  if (opCount) *opCount = b.opCount;
  return b.source.c_str();
}

int STENCIL_ABI(scriptOpCount, scriptOpCount)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->getOps().size()) : -1;
}

int STENCIL_ABI(scriptOpAt, scriptOpAt)(int h, int i, int* kind, int* block, int* editIndex,
                                        int* line, int* col, int* strCount, int* numCount) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const stencil::core::script::Op* op = scriptOpAtIndex(h, i);
  if (!op) return 0;
  if (kind) *kind = static_cast<int>(op->kind);
  if (block) *block = op->block;
  if (editIndex) *editIndex = op->editIndex;
  if (line) *line = op->line;
  if (col) *col = op->col;
  if (strCount) *strCount = static_cast<int>(op->strs.size());
  if (numCount) *numCount = static_cast<int>(op->nums.size());
  return 1;
}

const char* STENCIL_ABI(scriptOpStr, scriptOpStr)(int h, int i, int k) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const stencil::core::script::Op* op = scriptOpAtIndex(h, i);
  if (!op || k < 0 || k >= static_cast<int>(op->strs.size())) return nullptr;
  return op->strs[static_cast<std::size_t>(k)].c_str();
}

int STENCIL_ABI(scriptOpTokCount, scriptOpTokCount)(int h, int i) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const stencil::core::script::Op* op = scriptOpAtIndex(h, i);
  return op ? static_cast<int>(op->toks.size()) : -1;
}

// One unresolved length token, e.g. "10%" or "-1in"; see scriptOpResolve for pixels.
const char* STENCIL_ABI(scriptOpTok, scriptOpTok)(int h, int i, int k) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const stencil::core::script::Op* op = scriptOpAtIndex(h, i);
  if (!op || k < 0 || k >= static_cast<int>(op->toks.size())) return nullptr;
  return op->toks[static_cast<std::size_t>(k)].c_str();
}

int STENCIL_ABI(scriptOpNum, scriptOpNum)(int h, int i, int k, double* out) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const stencil::core::script::Op* op = scriptOpAtIndex(h, i);
  if (!op || k < 0 || k >= static_cast<int>(op->nums.size())) return 0;
  if (out) *out = op->nums[static_cast<std::size_t>(k)];
  return 1;
}

/* Resolves one op's length tokens into pixels against the CURRENT image size — a crop
 * changes it mid-script, so pass what you hold right now. Writes at most `cap` doubles and
 * returns the count; -1 on an unknown handle/index, -2 when `cap` is too small. */
int STENCIL_ABI(scriptOpResolve, scriptOpResolve)(int h, int i, double imageW, double imageH,
                                                  double pxPerCmX, double pxPerCmY, double* out,
                                                  int cap) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  if (!p) return -1;
  return stencil::core::script::resolveOp(*p, i, imageW, imageH, pxPerCmX, pxPerCmY, out, cap);
}

// The canonical dump, memoized on the handle. This is the fixture format.
const char* STENCIL_ABI(scriptDump, scriptDump)(int h) {
  std::lock_guard<std::mutex> hold(scriptLock());
  const auto* p = scriptTable().get(h);
  return p ? p->getDump().c_str() : nullptr;
}

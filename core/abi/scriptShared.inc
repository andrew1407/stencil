// The .stc script ABI, identical on both extern "C" surfaces. Included by wasmScriptApi.cpp
// and cliApi.cpp INSIDE their extern "C" block, each having defined
// STENCIL_ABI(wasmName, cliName) first. Editing this file changes BOTH ABIs.
#ifndef STENCIL_ABI
#  error "define STENCIL_ABI(wasmName, cliName) before including abi/scriptShared.inc"
#endif

/* Handle-owned strings: every const char* returned here points INTO the handle's own
 * memory and stays valid until scriptDestroy(h). This is the one exception to the
 * "returned strings are static" rule in cliApi.h. An unknown handle yields NULL / 0 / -1,
 * never a crash. */

// static, not an anonymous namespace: this file is included inside an extern "C" block.
static stencil::core::abi::HandleTable<stencil::core::script::ScriptProgram>& scriptTable() {
  static stencil::core::abi::HandleTable<stencil::core::script::ScriptProgram> table;
  return table;
}

static const stencil::core::script::Op* scriptOpAtIndex(int h, int i) {
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->ops().size())) return nullptr;
  return &p->ops()[static_cast<std::size_t>(i)];
}

// Parses `len` bytes and returns a handle, or 0 when the text pointer is null.
int STENCIL_ABI(scriptParse, scriptParse)(const char* text, int len) {
  if (!text || len < 0) return 0;
  const int h = scriptTable().create();
  auto* p = scriptTable().get(h);
  *p = stencil::core::script::ScriptProgram::parse(text, len);
  return h;
}

void STENCIL_ABI(scriptDestroy, scriptDestroy)(int h) { scriptTable().destroy(h); }

int STENCIL_ABI(scriptErrorCount, scriptErrorCount)(int h) {
  const auto* p = scriptTable().get(h);
  return p ? p->errorCount() : -1;
}

int STENCIL_ABI(scriptDiagCount, scriptDiagCount)(int h) {
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->diagnostics().size()) : -1;
}

// Returns the message; *sev is 0 error / 1 warning, and *code the stable code string.
const char* STENCIL_ABI(scriptDiagAt, scriptDiagAt)(int h, int i, int* sev, int* line,
                                                    int* col, int* len, const char** code) {
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->diagnostics().size())) return nullptr;
  const stencil::core::script::Diagnostic& d = p->diagnostics()[static_cast<std::size_t>(i)];
  if (sev) *sev = static_cast<int>(d.severity);
  if (line) *line = d.line;
  if (col) *col = d.col;
  if (len) *len = d.len;
  if (code) *code = d.code.c_str();
  return d.message.c_str();
}

int STENCIL_ABI(scriptTokenCount, scriptTokenCount)(int h) {
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->tokens().size()) : -1;
}

int STENCIL_ABI(scriptTokenAt, scriptTokenAt)(int h, int i, int* kind, int* line, int* col,
                                              int* len) {
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->tokens().size())) return 0;
  const stencil::core::script::Token& t = p->tokens()[static_cast<std::size_t>(i)];
  if (kind) *kind = static_cast<int>(t.kind);
  if (line) *line = t.line;
  if (col) *col = t.col;
  if (len) *len = t.len;
  return 1;
}

int STENCIL_ABI(scriptBlockCount, scriptBlockCount)(int h) {
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->blocks().size()) : -1;
}

// Returns the source spec ("" for the implicit project block).
const char* STENCIL_ABI(scriptBlockAt, scriptBlockAt)(int h, int i, int* kind, int* frame,
                                                      int* opStart, int* opCount) {
  const auto* p = scriptTable().get(h);
  if (!p || i < 0 || i >= static_cast<int>(p->blocks().size())) return nullptr;
  const stencil::core::script::Block& b = p->blocks()[static_cast<std::size_t>(i)];
  if (kind) *kind = static_cast<int>(b.kind);
  if (frame) *frame = b.frame;
  if (opStart) *opStart = b.opStart;
  if (opCount) *opCount = b.opCount;
  return b.source.c_str();
}

int STENCIL_ABI(scriptOpCount, scriptOpCount)(int h) {
  const auto* p = scriptTable().get(h);
  return p ? static_cast<int>(p->ops().size()) : -1;
}

int STENCIL_ABI(scriptOpAt, scriptOpAt)(int h, int i, int* kind, int* block, int* editIndex,
                                        int* line, int* col, int* strCount, int* numCount) {
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
  const stencil::core::script::Op* op = scriptOpAtIndex(h, i);
  if (!op || k < 0 || k >= static_cast<int>(op->strs.size())) return nullptr;
  return op->strs[static_cast<std::size_t>(k)].c_str();
}

int STENCIL_ABI(scriptOpNum, scriptOpNum)(int h, int i, int k, double* out) {
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
  const auto* p = scriptTable().get(h);
  if (!p) return -1;
  return stencil::core::script::resolveOp(*p, i, imageW, imageH, pxPerCmX, pxPerCmY, out, cap);
}

// The canonical dump, memoized on the handle. This is the fixture format.
const char* STENCIL_ABI(scriptDump, scriptDump)(int h) {
  const auto* p = scriptTable().get(h);
  return p ? p->dump().c_str() : nullptr;
}

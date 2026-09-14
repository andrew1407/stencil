// WebAssembly ABI for the .stc script engine; the bodies are shared verbatim with the CLI
// ABI through abi/scriptShared.inc. Driven by browser/tests/wasm-parity.test.js.

#include "HandleTable.hpp"
#include "scriptProgram.hpp"

#include <cstddef>

extern "C" {

#define STENCIL_ABI(wasmName, cliName) stencil_##wasmName
#include "scriptShared.inc"
#undef STENCIL_ABI

}  // extern "C"

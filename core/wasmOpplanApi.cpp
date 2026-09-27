// WebAssembly spelling of the op-plan ABI; the bodies are shared with the CLI ABI through
// abi/opplanShared.inc. Not in EXPORTED_FUNCTIONS: the browser walks the JS twin, so wasm-ld
// drops these and the module is unchanged. stencil_tests drives them natively.

#include "HandleTable.hpp"
#include "planSchema.hpp"
#include "planWalk.hpp"

#include <cstddef>
#include <mutex>
#include <string_view>

extern "C" {

#define STENCIL_ABI(wasmName, cliName) stencil_##wasmName
#include "opplanShared.inc"
#undef STENCIL_ABI

}  // extern "C"

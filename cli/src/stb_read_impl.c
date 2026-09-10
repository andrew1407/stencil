// stb_image (DECODER) — the untrusted-input side, so it is its own translation unit and
// keeps UBSan on (see build.zig). Only the formats the CLI actually reads are compiled in
// (image.zig's Format enum): the GIF/PSD/PIC/PNM/HDR codecs are where most of stb's
// historical memory-safety bugs live. STBI_NO_STDIO: we use the *_from_memory entry
// points, so no fopen surface. The dimension cap bounds w*h*4 before any allocation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"

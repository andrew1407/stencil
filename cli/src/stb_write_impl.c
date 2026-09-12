// stb_image_write (ENCODER) — our own pixels, not untrusted input. Its own translation
// unit because the JPEG encoder relies on signed-shift wraparound that is technically UB;
// it's benign in C but Zig instruments C with UBSan in Debug and would trap, so build.zig
// compiles THIS file (and only this file) with -fno-sanitize=undefined.
// STBI_WRITE_NO_STDIO: we use the *_to_func entry points, so no fopen surface.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

// Single translation unit that pulls in the stb_image / stb_image_write
// implementations. The Zig side (@cImport in image.zig) includes the same headers
// for declarations only. We use the *_from_memory / *_to_func entry points, so the
// stdio-backed paths are compiled out (smaller, no fopen surface).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

/* pystencil's side of the CLI's stb translation units (cli/src/media/stb_read_impl.c and
   stb_write_impl.c): the allocator hooks the decoder declares, libc here, and the C ABI
   pystencil/_ffi/stb.py binds. The decoder's narrowing stays in the CLI's file, stated once. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STBI_NO_STDIO
#define STBI_WRITE_NO_STDIO
#include <stb_image.h>
#include <stb_image_write.h>

/* The largest block the running decode may take (0 = no cap), per thread: a PNG stream that
   inflates past it fails there instead of growing until memory runs out. */
static _Thread_local size_t block_cap;
static _Thread_local int cap_hit;

static int over_cap(size_t n) {
  if (block_cap == 0 || n <= block_cap) return 0;
  cap_hit = 1;
  return 1;
}

/* Zeroed: a scan cut short at a restart marker leaves planes stb never writes, which would
   otherwise carry old heap bytes into the pixels. */
void *stencil_stbi_alloc(size_t n) { return over_cap(n) ? NULL : calloc(1, n); }
void *stencil_stbi_realloc(void *p, size_t n) { return over_cap(n) ? NULL : realloc(p, n); }
void stencil_stbi_free(void *p) { free(p); }

/* A w*h*4 RGBA8 plane for stencil_py_free, or NULL with *error naming stb's reason. */
unsigned char *stencil_py_decode_rgba(const unsigned char *data, int len, size_t max_block,
                                      int *width, int *height, const char **error) {
  int channels = 0;
  block_cap = max_block;
  cap_hit = 0;
  unsigned char *pixels = stbi_load_from_memory(data, len, width, height, &channels, 4);
  block_cap = 0;
  if (pixels) *error = NULL;
  else *error = cap_hit ? "its pixel data runs past the plane its header claims"
                        : stbi_failure_reason();
  return pixels;
}

typedef struct {
  unsigned char *bytes;
  size_t len, cap;
  int failed;
} Sink;

static void sink_write(void *context, void *data, int size) {
  Sink *sink = (Sink *)context;
  if (sink->failed || size <= 0) return;
  size_t need = sink->len + (size_t)size;
  if (need > sink->cap) {
    size_t cap = sink->cap ? sink->cap : 4096;
    while (cap < need) cap *= 2;
    unsigned char *grown = realloc(sink->bytes, cap);
    if (!grown) {
      sink->failed = 1;
      return;
    }
    sink->bytes = grown;
    sink->cap = cap;
  }
  memcpy(sink->bytes + sink->len, data, (size_t)size);
  sink->len = need;
}

/* RGBA8 in, JPEG out with the alpha dropped, as stb writes it; NULL when stb refuses. */
unsigned char *stencil_py_encode_jpeg(const unsigned char *rgba, int width, int height,
                                      int quality, int *out_len) {
  Sink sink = {NULL, 0, 0, 0};
  int ok = stbi_write_jpg_to_func(sink_write, &sink, width, height, 4, rgba, quality);
  if (!ok || sink.failed || sink.len > INT_MAX) {
    free(sink.bytes);
    return NULL;
  }
  *out_len = (int)sink.len;
  return sink.bytes;
}

void stencil_py_free(void *p) { free(p); }

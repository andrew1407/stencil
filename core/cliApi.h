#pragma once

/* extern "C" surface over the shared core for the Zig CLI (cli/) and pystencil. Mirrors
 * the role of wasmApi.cpp for the browser, but shaped for a native image pipeline: it
 * operates on caller-owned interleaved RGBA8 buffers (byte order R,G,B,A) and plain C
 * strings. The host owns all allocation, file/codec/video I/O and JSON parsing; this ABI
 * only transforms buffers and parses geometry / colour / length tokens.
 *
 * Buffer contract — the ABI trusts these and bounds-checks NOTHING:
 *   - A `w`/`h` (or `width`/`height`) pixel buffer is exactly w*h*4 bytes; a
 *     `pixelCount` buffer is pixelCount*4 bytes. Rows are contiguous, no stride.
 *   - A `pts` array is 2*nPts doubles, [x0,y0,x1,y1,…].
 *   - A `luma` plane is width*height bytes (1 per pixel).
 *   - Row-range calls take a half-open [y0,y1); see the Row ranges section for which
 *     ones clamp and which trust y1.
 *   - `src` and `dst` must not overlap; in-place ops say so and take one buffer.
 * Ownership: nothing here allocates or frees caller memory, and no pointer argument is
 * retained past the call — strings are read (and copied if needed) before returning.
 * Any `const char*` in is NUL-terminated or NULL (NULL reads as ""); every `const char*`
 * returned is static storage the caller must not free. Out-pointers may be NULL (the
 * value is simply not written) unless a function says otherwise; on a 0/failure return
 * the out-pointers are left untouched.
 * The one exception is the stencil_cli_script* family: its strings point INTO their script
 * handle and stay valid until scriptDestroy(h) — never free one, never read one after that.
 * An unknown handle returns NULL / 0 / -1, never a crash. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CSS colour (named / #rgb / #rgba / #rrggbb / #rrggbbaa / "transparent") -> 0..255
 * channels. 1 on success, 0 if unrecognized. */
int stencil_cli_parseColor(const char* spec, int* r, int* g, int* b, int* a);

/* ISO A/B/C page name -> cm. 1 if known, else 0. */
int stencil_cli_namedPageSize(const char* name, double* wcm, double* hcm);

/* Space-separated page-format names in A/B/C-series order, no "custom". */
const char* stencil_cli_pageFormats(void);

/* Page (cm) rendered at `dpi` (CSS 96 default). */
void stencil_cli_defaultBlankSizePx(double pageWcm, double pageHcm, double dpi,
                                    int* outW, int* outH);

/* Crop string ("x1 = .. x2 = .. y1 = .. y2 = ..") -> clamped integer pixel rect;
 * `album` (0/1) drives single-axis derivation. 1 on success, 0 on a bad spec / empty. */
int stencil_cli_resolveCrop(const char* spec, double imageW, double imageH,
                            double pxPerCmX, double pxPerCmY,
                            double pageWcm, double pageHcm, int album,
                            int* outX, int* outY, int* outW, int* outH);

/* dst is rw*rh*4 bytes and always written in full: the rect may hang off src, and
 * pixels outside it are zero-filled (transparent). */
void stencil_cli_cropImageRGBA(const uint8_t* src, int srcW, int srcH,
                               int rx, int ry, int rw, int rh, uint8_t* dst);

/* Signed quarter-turn count -> 0..3 clockwise. */
int stencil_cli_normalizeQuarters(int quarters);

void stencil_cli_rotatedDims(int w, int h, int quarters, int* outW, int* outH);

/* dst is sized by stencil_cli_rotatedDims: still w*h*4 bytes, dims swapped on odd turns. */
void stencil_cli_rotateImageRGBA(const uint8_t* src, int w, int h, int quarters,
                                 uint8_t* dst);

void stencil_cli_fillRGBA(uint8_t* dst, int pixelCount, int r, int g, int b, int a);

/* Half-open [y0,y1) row slices of the whole-buffer ops, for a caller-owned thread pool
 * (core owns no threading policy); the same bytes as the whole-image call. Ranges clamp
 * to the image, EXCEPT stencil_cli_applyFilterRows: it is not told the height, so its
 * y1 is trusted — pass rows that exist. */
void stencil_cli_cropImageRows(const uint8_t* src, int srcW, int srcH,
                               int rx, int ry, int rw, int rh, uint8_t* dst,
                               int dy0, int dy1);
void stencil_cli_rotateImageRows(const uint8_t* src, int w, int h, int quarters,
                                 uint8_t* dst, int oy0, int oy1);
void stencil_cli_applyFilterRows(const char* mode, uint8_t* data, int width,
                                 int y0, int y1, int tintR, int tintG, int tintB);
/* Contour in two passes over a caller-owned `luma` plane. The Sobel pass reads one row
 * OUTSIDE its range on each side, so every luma row must be built before any sobel
 * row runs — two phases, never interleaved. */
void stencil_cli_buildLumaRows(const uint8_t* data, int width, int height,
                               int y0, int y1, uint8_t* luma);
void stencil_cli_sobelRows(const uint8_t* luma, uint8_t* data, int width, int height,
                           int y0, int y1);

/* In place. `mode` is "none" | "bw" | "sepia" | "invert" | any other (a custom duotone
 * toward tintR,tintG,tintB). "contour" is a no-op here — it needs dimensions, use
 * stencil_cli_applyContour. */
void stencil_cli_applyFilter(const char* mode, uint8_t* data, int pixelCount,
                             int tintR, int tintG, int tintB);

/* Sobel edges in place: dark on a white page, alpha preserved. */
void stencil_cli_applyContour(uint8_t* data, int width, int height);

/* Burn one polyline into the buffer in place. `style` is "solid"|"dashed"|"dotted";
 * `locked` (0/1) closes the shape and enables the `fillColor` fill. Colours are CSS
 * strings (see parseColor); `pointColor` NULL or "" inherits `color`. */
void stencil_cli_rasterizeLine(uint8_t* buf, int w, int h,
                               const double* pts, int nPts,
                               const char* color, double thickness, double pointSize,
                               const char* style, int locked, const char* fillColor,
                               const char* pointColor);

/* `var` is the ASCII code of the single variable ('x' or 'y'); an empty `expr` is the
 * identity. applyFormula returns `value` unchanged when allowFormulas==0 or evaluation
 * fails / is non-finite (identity-on-error, like the browser). */
int stencil_cli_validateFormula(const char* expr, int var);
double stencil_cli_applyFormula(const char* expr, int var, double value, int allowFormulas);

/* CSS colour keywords, alphabetically, for adapter drift guards: NULL out of range,
 * 0xRRGGBB written to *rgb. */
int stencil_cli_colorNameCount(void);
const char* stencil_cli_colorNameAt(int index, unsigned int* rgb);

/* Space-separated `expire` vocabulary in help order; the console prints its help from these. */
const char* stencil_cli_durationUnits(void);
const char* stencil_cli_durationOffAliases(void);

/* "days 23" / "fortnight" / "off" -> ms in *outMs (0 = keep forever), which the caller
 * adds to "now". 1 on a valid spec, 0 otherwise. */
int stencil_cli_parseDuration(const char* spec, long long* outMs);

/* .stc scripts: parse once into a handle, then read diagnostics, tokens, blocks and the
 * lowered ops back out. Normative in stc-contract/stc-contract.md; strings are handle-owned. */
int stencil_cli_scriptParse(const char* text, int len);
void stencil_cli_scriptDestroy(int h);
int stencil_cli_scriptErrorCount(int h);

int stencil_cli_scriptDiagCount(int h);
const char* stencil_cli_scriptDiagAt(int h, int i, int* sev, int* line, int* col, int* len,
                                     const char** code);

int stencil_cli_scriptTokenCount(int h);
int stencil_cli_scriptTokenAt(int h, int i, int* kind, int* line, int* col, int* len);

int stencil_cli_scriptBlockCount(int h);
const char* stencil_cli_scriptBlockAt(int h, int i, int* kind, int* frame, int* opStart,
                                      int* opCount);

int stencil_cli_scriptOpCount(int h);
int stencil_cli_scriptOpAt(int h, int i, int* kind, int* block, int* editIndex, int* line,
                           int* col, int* strCount, int* numCount);
const char* stencil_cli_scriptOpStr(int h, int i, int k);
int stencil_cli_scriptOpNum(int h, int i, int k, double* out);

/* Length tokens -> pixels against the CURRENT image size; see abi/scriptShared.inc for the
 * per-kind layout. Returns the count written, -1 unknown handle/index, -2 when cap is small. */
int stencil_cli_scriptOpResolve(int h, int i, double imageW, double imageH, double pxPerCmX,
                                double pxPerCmY, double* out, int cap);

const char* stencil_cli_scriptDump(int h);

#ifdef __cplusplus
}  /* extern "C" */
#endif

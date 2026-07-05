#ifndef STENCIL_CORE_CLIAPI_H
#define STENCIL_CORE_CLIAPI_H

/* extern "C" surface over the shared core for the Zig CLI (cli/). Mirrors the role of
 * wasmApi.cpp for the browser, but shaped for a native image pipeline: it operates on
 * caller-owned interleaved RGBA8 buffers (byte order R,G,B,A) and plain C strings.
 * The host (Zig) owns all allocation, file/codec/video I/O and JSON parsing; this ABI
 * only transforms buffers and parses geometry / colour / length tokens. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Colour ─────────────────────────────────────────────────────────────────── */
/* Parse a CSS colour (named / #rgb / #rgba / #rrggbb / #rrggbbaa / "transparent").
 * Writes 0..255 channels to *r,*g,*b,*a. Returns 1 on success, 0 if unrecognized. */
int stencil_cli_parseColor(const char* spec, int* r, int* g, int* b, int* a);

/* ── Page sizing ────────────────────────────────────────────────────────────── */
/* Named page size in cm (any ISO A/B/C name, e.g. "A4" or "B5"). Writes *wcm,*hcm;
 * returns 1 if known, else 0. */
int stencil_cli_namedPageSize(const char* name, double* wcm, double* hcm);

/* Space-separated canonical page-format names ("A0 A1 ... A10 B0 ... C10", no
 * "custom") in the canonical A/B/C-series order. Static storage — do not free. */
const char* stencil_cli_pageFormats(void);

/* Default blank-image pixel size for a page (cm) rendered at `dpi` (CSS 96 default). */
void stencil_cli_defaultBlankSizePx(double pageWcm, double pageHcm, double dpi,
                                    int* outW, int* outH);

/* ── Crop ───────────────────────────────────────────────────────────────────── */
/* Resolve a crop string ("x1 = .. x2 = .. y1 = .. y2 = ..") against an image of
 * imageW x imageH px (with px-per-cm and page-cm dimensions for unit/album handling).
 * `album` (0/1) drives single-axis derivation. Writes a clamped integer pixel rect to
 * *outX,*outY,*outW,*outH. Returns 1 on success, 0 on a bad spec / empty result. */
int stencil_cli_resolveCrop(const char* spec, double imageW, double imageH,
                            double pxPerCmX, double pxPerCmY,
                            double pageWcm, double pageHcm, int album,
                            int* outX, int* outY, int* outW, int* outH);

/* ── RGBA8 buffer transforms ────────────────────────────────────────────────── */
/* Copy sub-rectangle (rx,ry,rw,rh) of src (srcW x srcH) into dst (rw*rh*4 bytes). */
void stencil_cli_cropImageRGBA(const uint8_t* src, int srcW, int srcH,
                               int rx, int ry, int rw, int rh, uint8_t* dst);

/* Normalize a signed quarter-turn count to 0..3 (clockwise). */
int stencil_cli_normalizeQuarters(int quarters);

/* Output dims after rotating w x h by `quarters` quarter-turns (writes *outW,*outH). */
void stencil_cli_rotatedDims(int w, int h, int quarters, int* outW, int* outH);

/* Rotate src (w x h) by `quarters` quarter-turns clockwise into dst (rotated dims). */
void stencil_cli_rotateImageRGBA(const uint8_t* src, int w, int h, int quarters,
                                 uint8_t* dst);

/* Fill `pixelCount` RGBA8 pixels of dst with one colour. */
void stencil_cli_fillRGBA(uint8_t* dst, int pixelCount, int r, int g, int b, int a);

/* ── Filter ─────────────────────────────────────────────────────────────────── */
/* Apply an image filter in place to a `pixelCount`-pixel RGBA8 buffer. `mode` is
 * "none" | "bw" | "sepia" | "invert" | any other (a custom duotone toward
 * tintR,tintG,tintB). "contour" is a no-op here — it needs dimensions, use
 * stencil_cli_applyContour below. */
void stencil_cli_applyFilter(const char* mode, uint8_t* data, int pixelCount,
                             int tintR, int tintG, int tintB);

/* Sobel edge detection ("contour") in place on a width x height RGBA8 buffer:
 * dark edges on a white page, alpha preserved. Null data / non-positive
 * dimensions are a no-op. */
void stencil_cli_applyContour(uint8_t* data, int width, int height);

/* ── Rasterise a layout line ────────────────────────────────────────────────── */
/* Burn one polyline into an RGBA8 buffer (w x h). `pts` holds nPts (x,y) pairs
 * (2*nPts doubles). `style` is "solid"|"dashed"|"dotted"; `locked` (0/1) closes the
 * shape and enables the `fillColor` fill. Colours are CSS strings (see parseColor). */
void stencil_cli_rasterizeLine(uint8_t* buf, int w, int h,
                               const double* pts, int nPts,
                               const char* color, double thickness, double markerSize,
                               const char* style, int locked, const char* fillColor);

/* ── Coordinate-transform formula (same FormulaParser the browser applies) ────── */
/* `var` is the ASCII code of the single variable ('x' or 'y'). */
/* Validate a formula: 1 if valid (empty = identity), else 0. */
int stencil_cli_validateFormula(const char* expr, int var);
/* Apply `expr` to `value`. Returns `value` unchanged when allowFormulas==0, `expr` is
 * empty, or evaluation fails / is non-finite (identity-on-error, like the browser). */
double stencil_cli_applyFormula(const char* expr, int var, double value, int allowFormulas);

/* ── Human-duration parser (same DurationParser the browser `expire` uses) ────── */
/* Parse a spec ("days 23", "fortnight", "month", "off") into milliseconds written to
 * *outMs (0 for off/never). Returns 1 on a valid spec, 0 otherwise (*outMs untouched).
 * The caller adds *outMs to "now" to get the expiry timestamp. */
int stencil_cli_parseDuration(const char* spec, long long* outMs);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* STENCIL_CORE_CLIAPI_H */

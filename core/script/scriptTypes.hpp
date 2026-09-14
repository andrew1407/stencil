#pragma once
#include <string>
#include <vector>

// Value types of the .stc script engine. Port target: browser/js/core/scriptTypes.js.
// The grammar itself is normative in contracts/stc/stc-contract.md.
namespace stencil::core::script {

  // Caps. Identical in the JS port so wasm and the fallback reject the same inputs.
  inline constexpr int MAX_LINES = 20000;
  inline constexpr int MAX_TOKENS = 200000;
  inline constexpr int MAX_OPS = 5000;
  inline constexpr int MAX_BLOCKS = 256;
  inline constexpr int MAX_TEMPLATES = 256;
  inline constexpr int MAX_TEMPLATE_DEPTH = 16;
  // Every op sits under at most MAX_TEMPLATE_DEPTH expansions, so a script within MAX_OPS
  // never reaches this; only a fan-out that yields no op at all can.
  inline constexpr int MAX_TEMPLATE_EXPANSIONS = MAX_OPS * MAX_TEMPLATE_DEPTH;
  inline constexpr int MAX_POINTS_PER_LINE = 200;
  inline constexpr int MAX_SOURCE_CHARS = 1024;

  // Editor colouring classes. Crossing the ABI as ints: never reorder, only append.
  enum class TokenKind {
    COMMENT = 0,
    DIRECTIVE = 1,
    KEYWORD = 2,
    NUMBER = 3,
    UNIT = 4,
    COLOR = 5,
    STRING = 6,
    PARAM = 7,
    PUNCT = 8,
    IDENT = 9,
    ERROR = 10,
  };

  struct Token {
    int line = 1;  // 1-based
    int col = 1;   // 1-based, in bytes
    int len = 0;
    TokenKind kind = TokenKind::IDENT;
    std::string text;  // the slice, lower-cased only where the grammar is case-blind
  };

  enum class Severity { ERROR = 0, WARNING = 1 };

  struct Diagnostic {
    Severity severity = Severity::ERROR;
    std::string code;  // stable, e.g. "E_UNKNOWN_DIRECTIVE" — fixtures key off this
    int line = 1;
    int col = 1;
    int len = 0;
    std::string message;
  };

  // What a block's ops run against. OPEN carries the spec; the adapter resolves it.
  enum class SourceKind { PROJECT = 0, FILE = 1, URL = 2, DIR = 3, GLOB = 4 };

  // Crossing the ABI as ints: never reorder, only append.
  enum class OpKind {
    OPEN = 0,
    FRAME = 1,
    CROP = 2,
    FILTER = 3,
    LINE = 4,
    RECT = 5,
    LAYOUT = 6,
    SAVE = 7,
    UNDO = 8,
    REDO = 9,
  };

  /* One lowered operation: strs plain strings, toks length tokens scriptOpResolve() turns
   * into pixels, nums plain numbers. Per kind — OPEN strs{source} nums{sourceKind} · FRAME
   * nums{index} · CROP strs{aspect} toks{x1,x2,y1,y2} nums{album} · FILTER strs{mode,tint} ·
   * LINE/RECT strs{color,style,fillColor,pointColor} toks{x0,y0,…} nums{thickness,pointSize,
   * locked} · LAYOUT strs{source,mode} nums{sourceKind} · SAVE strs{target} · UNDO/REDO
   * nums{steps}. RECT is a LINE with locked = 1. */
  struct Op {
    OpKind kind = OpKind::CROP;
    int block = 0;
    int line = 1;
    int col = 1;
    int len = 0;
    int editIndex = 0;  // 1-based within the block; 0 for a non-edit op
    std::vector<std::string> strs;
    std::vector<std::string> toks;
    std::vector<double> nums;
  };

  struct Block {
    std::string source;  // "" for the implicit project block
    SourceKind kind = SourceKind::PROJECT;
    int frame = 0;  // reserved: exported for the adapters, never assigned by the lowerer
    int opStart = 0;
    int opCount = 0;
    int line = 1;
  };

  // The line style `@use line` accumulates, applied to every @line / @rect after it.
  struct LineStyle {
    std::string color = "#FFFF00";
    std::string style = "solid";
    std::string fillColor = "transparent";
    std::string pointColor;
    double thickness = 2.0;
    double pointSize = 4.0;
  };

}  // namespace stencil::core::script

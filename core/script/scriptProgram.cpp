#include "scriptProgram.hpp"

#include "cropSpec.hpp"
#include "lengthTokens.hpp"
#include "scriptDiagnostics.hpp"
#include "scriptDump.hpp"
#include "scriptLexer.hpp"
#include "scriptLower.hpp"
#include "scriptParser.hpp"

#include <algorithm>

namespace stencil::core::script {

  ScriptProgram ScriptProgram::parse(const char* text, int len) {
    ScriptProgram p;
    LexResult lexed = lexScript(text, len);
    p.tokens_ = lexed.tokens;

    ParseResult parsed = parseScript(lexed.tokens);
    parsed.diagnostics.insert(parsed.diagnostics.begin(), lexed.diagnostics.begin(),
                              lexed.diagnostics.end());

    LowerResult lowered = lowerScript(parsed);
    // Report in source order; lexing, parsing and lowering each find their own.
    std::stable_sort(lowered.diagnostics.begin(), lowered.diagnostics.end(),
                     [](const Diagnostic& a, const Diagnostic& b) {
                       if (a.line != b.line) return a.line < b.line;
                       return a.col < b.col;
                     });
    p.diagnostics_ = lowered.diagnostics;
    p.blocks_ = lowered.blocks;
    p.ops_ = lowered.ops;
    return p;
  }

  bool ScriptProgram::hasErrors() const { return stencil::core::script::hasErrors(diagnostics_); }

  int ScriptProgram::errorCount() const {
    int n = 0;
    for (const Diagnostic& d : diagnostics_)
      if (d.severity == Severity::ERROR) ++n;
    return n;
  }

  const std::string& ScriptProgram::dump() const {
    if (!dumped_) {
      dump_ = dumpProgram(*this);
      dumped_ = true;
    }
    return dump_;
  }

  namespace {

    // A bare "-" token means "from the far edge", which resolveAxisPx already applies.
    bool axis(const std::string& tok, double lengthPx, double pxPerCm, double& out) {
      const auto v = resolveAxisPx(tok, lengthPx, pxPerCm, 0.0);
      if (!v) return false;
      out = *v;
      return true;
    }

    int resolveShape(const Op& op, double w, double h, double pxX, double pxY, double* out,
                     int cap) {
      const std::size_t pointCount = op.toks.size() / 2;
      const bool expand = op.kind == OpKind::RECT && pointCount == 2;
      const std::size_t outPoints = expand ? 4 : pointCount;
      const int need = static_cast<int>(outPoints) * 2 + 2;
      if (cap < need) return -2;

      std::vector<double> xs, ys;
      for (std::size_t i = 0; i < pointCount; ++i) {
        double x = 0.0, y = 0.0;
        if (!axis(op.toks[i * 2], w, pxX, x)) return -1;
        if (!axis(op.toks[i * 2 + 1], h, pxY, y)) return -1;
        xs.push_back(x);
        ys.push_back(y);
      }
      if (expand) {  // two opposite corners become a closed rectangle
        const double x0 = xs[0], y0 = ys[0], x1 = xs[1], y1 = ys[1];
        xs = {x0, x1, x1, x0};
        ys = {y0, y0, y1, y1};
      }
      int n = 0;
      for (std::size_t i = 0; i < xs.size(); ++i) {
        out[n++] = xs[i];
        out[n++] = ys[i];
      }
      out[n++] = op.nums.size() > 0 ? op.nums[0] : 2.0;
      out[n++] = op.nums.size() > 1 ? op.nums[1] : 4.0;
      return n;
    }

  }  // namespace

  int resolveOp(const ScriptProgram& program, int index, double imageW, double imageH,
                double pxPerCmX, double pxPerCmY, double* out, int cap) {
    if (index < 0 || index >= static_cast<int>(program.ops().size())) return -1;
    if (!out || cap < 0) return -2;
    const Op& op = program.ops()[static_cast<std::size_t>(index)];

    switch (op.kind) {
      case OpKind::CROP: {
        if (cap < 4) return -2;
        CropSpec spec;
        if (!op.toks[0].empty()) spec.x1 = op.toks[0];
        if (!op.toks[1].empty()) spec.x2 = op.toks[1];
        if (!op.toks[2].empty()) spec.y1 = op.toks[2];
        if (!op.toks[3].empty()) spec.y2 = op.toks[3];
        if (!op.strs.empty() && !op.strs[0].empty()) spec.aspect = op.strs[0];
        CropResolveParams params;
        params.imageW = imageW;
        params.imageH = imageH;
        params.pxPerCmX = pxPerCmX;
        params.pxPerCmY = pxPerCmY;
        params.pageWidth = pxPerCmX > 0.0 ? imageW / pxPerCmX : 0.0;
        params.pageHeight = pxPerCmY > 0.0 ? imageH / pxPerCmY : 0.0;
        const bool album = !op.nums.empty() && op.nums[0] != 0.0;
        const auto rect = resolveCropRect(spec, params, album);
        if (!rect) return -1;
        out[0] = rect->x;
        out[1] = rect->y;
        out[2] = rect->width;
        out[3] = rect->height;
        return 4;
      }
      case OpKind::LINE:
      case OpKind::RECT:
        return resolveShape(op, imageW, imageH, pxPerCmX, pxPerCmY, out, cap);
      case OpKind::FRAME:
      case OpKind::UNDO:
      case OpKind::REDO: {
        if (cap < 1) return -2;
        out[0] = op.nums.empty() ? 0.0 : op.nums[0];
        return 1;
      }
      default:
        return 0;
    }
  }

}  // namespace stencil::core::script

#include "opPlan.hpp"

#include "colorNames.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include <cmath>

namespace stencil::llm {

  namespace {

    // ── extraction ───────────────────────────────────────────────────────────

    // Remove Markdown code-fence points (``` with an optional language tag) so
    // a fenced JSON block parses like bare JSON.
    QString stripFences(QString text) {
      static const QRegularExpression fence(QStringLiteral("```[A-Za-z]*"));
      return text.remove(fence);
    }

    // First balanced { … } substring that parses as a JSON OBJECT (brace
    // counting skips string literals, so braces inside "reply" don't confuse
    // it). Chat text containing incidental balanced braces that aren't JSON is
    // skipped over rather than failing the turn.
    bool extractFirstObject(const QString& text, QJsonObject& out) {
      const int n = text.size();
      for (int start = text.indexOf(QLatin1Char('{')); start >= 0;
           start = text.indexOf(QLatin1Char('{'), start + 1)) {
        int depth = 0;
        bool inStr = false, esc = false;
        for (int i = start; i < n; ++i) {
          const QChar c = text.at(i);
          if (inStr) {
            if (esc) esc = false;
            else if (c == QLatin1Char('\\')) esc = true;
            else if (c == QLatin1Char('"')) inStr = false;
            continue;
          }
          if (c == QLatin1Char('"')) {
            inStr = true;
          } else if (c == QLatin1Char('{')) {
            ++depth;
          } else if (c == QLatin1Char('}')) {
            if (--depth == 0) {
              const QJsonDocument doc =
                  QJsonDocument::fromJson(text.mid(start, i - start + 1).toUtf8());
              if (doc.isObject()) {
                out = doc.object();
                return true;
              }
              break;  // balanced but not JSON — try the next '{'
            }
          }
        }
      }
      return false;
    }

    // ── field validators ─────────────────────────────────────────────────────

    // Reject unknown keys on an action/line object (strict per contract §1).
    // Returns the first offending key, or empty when all keys are allowed.
    QString unknownKey(const QJsonObject& o, std::initializer_list<const char*> allowed) {
      for (auto it = o.begin(); it != o.end(); ++it) {
        bool ok = false;
        for (const char* k : allowed)
          if (it.key() == QLatin1String(k)) { ok = true; break; }
        if (!ok) return it.key();
      }
      return QString();
    }

    bool asInt(const QJsonValue& v, int& out) {
      if (!v.isDouble()) return false;
      const double d = v.toDouble();
      if (d != std::floor(d) || d < -2147483648.0 || d > 2147483647.0) return false;
      out = static_cast<int>(d);
      return true;
    }

    // cropSpec token: optional '-', number, optional unit % / px / cm / in
    // (bare number = px). Contract §2 — deliberately NOT the full core
    // lengthTokens grammar (no mm).
    bool isCropToken(const QString& t) {
      static const QRegularExpression re(
          QStringLiteral("^-?(\\d+(\\.\\d+)?|\\.\\d+)(%|px|cm|in)?$"));
      return t.size() <= kMaxSpecChars && re.match(t).hasMatch();
    }

    bool isHexColor(const QString& t) {
      static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
      return re.match(t).hasMatch();
    }

    // "#rrggbb" or a CSS colour name the core recognizes — the colour rule
    // shared by the blank and lineStyle ops.
    bool isColorNameOrHex(const QString& t) {
      return t.startsWith(QLatin1Char('#'))
                 ? isHexColor(t)
                 : core::parseColor(t.toStdString()).has_value();
    }

    // Lowercase ISO page name: a0…a10, b0…b10, c0…c10 (contract §2).
    bool isPageFormat(const QString& t) {
      static const QRegularExpression re(QStringLiteral("^[abc](10|[0-9])$"));
      return re.match(t).hasMatch();
    }

    // Formula charset [0-9xy+\-*/(). *] with the single variable matching the
    // axis (contract §2); the core formula engine validates the grammar again
    // before use (planExecutor).
    bool isFormulaExpr(const QString& expr, QChar axis) {
      if (expr.size() > kMaxSpecChars) return false;
      for (const QChar c : expr) {
        if (c.isDigit()) continue;
        if (c == QLatin1Char('x') || c == QLatin1Char('y')) {
          if (c != axis) return false;
          continue;
        }
        static const QString ops = QStringLiteral("+-*/(). ");
        if (!ops.contains(c)) return false;
      }
      return true;
    }

    // ── per-op parsing (strict; false ⇒ the whole plan fails) ────────────────

    bool err(QString* out, const QString& msg) {
      if (out) *out = msg;
      return false;
    }

    bool parseCrop(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "spec", "aspect"}); !k.isEmpty())
        return err(e, QStringLiteral("crop: unknown field \"%1\"").arg(k));
      if (!o.value("spec").isObject())
        return err(e, QStringLiteral("crop: \"spec\" must be an object"));
      const QJsonObject spec = o.value("spec").toObject();
      if (const QString k = unknownKey(spec, {"x1", "x2", "y1", "y2", "aspect"}); !k.isEmpty())
        return err(e, QStringLiteral("crop: unknown spec key \"%1\"").arg(k));
      const auto edge = [&](const char* key, QString& out) {
        const QJsonValue v = spec.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isString()) return false;
        const QString token = v.toString().trimmed();
        if (!isCropToken(token)) return false;
        out = token;
        return true;
      };
      if (!edge("x1", a.x1) || !edge("x2", a.x2) || !edge("y1", a.y1) || !edge("y2", a.y2))
        return err(e, QStringLiteral("crop: bad edge token"));
      // Optional aspect: strict "W:H", digits only, both parts positive (browser
      // CROP_ASPECT parity — untrimmed, so " 4:3 " fails like any bad token). The
      // ratio itself is resolved by core resolveCropRect at execution time.
      // Accepted inside the spec (canonical) or at the ACTION level beside it —
      // the §3.2 tolerance for models that misplace the key — folding into the
      // spec only when the spec lacks it; conflicting duplicates fail the plan.
      const auto aspectToken = [](const QJsonValue& v, QString& out) {
        static const QRegularExpression aspectRe(QStringLiteral("^(\\d+):(\\d+)$"));
        const QString token = v.isString() ? v.toString() : QString();
        const auto m = aspectRe.match(token);
        if (!m.hasMatch() || m.captured(1).toDouble() <= 0 || m.captured(2).toDouble() <= 0)
          return false;
        out = token;
        return true;
      };
      QString specAspect, actionAspect;
      if (const QJsonValue v = spec.value(QLatin1String("aspect")); !v.isUndefined()) {
        if (!aspectToken(v, specAspect))
          return err(e, QStringLiteral("crop: bad token for \"aspect\""));
      }
      if (const QJsonValue v = o.value(QLatin1String("aspect")); !v.isUndefined()) {
        if (!aspectToken(v, actionAspect))
          return err(e, QStringLiteral("crop: bad token for \"aspect\""));
      }
      if (!specAspect.isEmpty() && !actionAspect.isEmpty() && specAspect != actionAspect)
        return err(e, QStringLiteral(
                          "crop: conflicting \"aspect\" inside and beside \"spec\""));
      a.aspect = specAspect.isEmpty() ? actionAspect : specAspect;
      if (spec.isEmpty() && a.aspect.isEmpty())
        return err(e, QStringLiteral("crop: spec needs at least one of x1/x2/y1/y2/aspect"));
      return true;
    }

    bool parseRotate(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "dir", "times"}); !k.isEmpty())
        return err(e, QStringLiteral("rotate: unknown field \"%1\"").arg(k));
      const QString dir = o.value("dir").toString();
      if (dir != "left" && dir != "right")
        return err(e, QStringLiteral("rotate: dir must be \"left\" or \"right\""));
      a.rotateLeft = dir == "left";
      a.times = 1;
      if (o.contains("times")) {
        if (!asInt(o.value("times"), a.times) || a.times < 1 || a.times > 3)
          return err(e, QStringLiteral("rotate: times must be an integer 1..3"));
      }
      return true;
    }

    bool parseFilter(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "mode", "tint"}); !k.isEmpty())
        return err(e, QStringLiteral("filter: unknown field \"%1\"").arg(k));
      static const QStringList modes = {"none", "bw", "sepia", "invert", "contour", "custom"};
      a.mode = o.value("mode").toString();
      if (!modes.contains(a.mode))
        return err(e, QStringLiteral("filter: unknown mode \"%1\"").arg(a.mode));
      const bool custom = a.mode == "custom";
      if (custom != o.contains("tint"))
        return err(e, custom ? QStringLiteral("filter: \"custom\" requires a tint")
                             : QStringLiteral("filter: tint is only valid with \"custom\""));
      if (custom) {
        a.tint = o.value("tint").toString();
        if (!isHexColor(a.tint))
          return err(e, QStringLiteral("filter: tint must be \"#rrggbb\""));
      }
      return true;
    }

    bool parseLine(const QJsonValue& v, core::Line& line, QString* e) {
      if (!v.isObject()) return err(e, QStringLiteral("layout: line must be an object"));
      const QJsonObject o = v.toObject();
      if (const QString k = unknownKey(o, {"points", "color", "thickness", "pointSize",
                                           "style", "locked", "fillColor"});
          !k.isEmpty())
        return err(e, QStringLiteral("layout: unknown line field \"%1\"").arg(k));
      if (!o.value("points").isArray() || o.value("points").toArray().isEmpty())
        return err(e, QStringLiteral("layout: line needs a non-empty \"points\" array"));
      for (const QJsonValue& pv : o.value("points").toArray()) {
        if (!pv.isObject()) return err(e, QStringLiteral("layout: point must be an object"));
        const QJsonObject po = pv.toObject();
        if (const QString k = unknownKey(po, {"x", "y"}); !k.isEmpty())
          return err(e, QStringLiteral("layout: unknown point field \"%1\"").arg(k));
        if (!po.value("x").isDouble() || !po.value("y").isDouble())
          return err(e, QStringLiteral("layout: point x/y must be numbers"));
        line.points.push_back({po.value("x").toDouble(), po.value("y").toDouble()});
      }
      // Per-line defaults when a field is omitted (contract §3).
      const QJsonValue color = o.value("color");
      if (!color.isUndefined() && !color.isString())
        return err(e, QStringLiteral("layout: color must be a string"));
      line.color = color.toString(QStringLiteral("#FFFF00")).toStdString();
      const QJsonValue thick = o.value("thickness");
      if (!thick.isUndefined() && (!thick.isDouble() || thick.toDouble() <= 0))
        return err(e, QStringLiteral("layout: thickness must be a positive number"));
      line.thickness = thick.toDouble(2.0);
      const QJsonValue ps = o.value("pointSize");
      if (!ps.isUndefined() && (!ps.isDouble() || ps.toDouble() <= 0))
        return err(e, QStringLiteral("layout: pointSize must be a positive number"));
      line.pointSize = ps.toDouble(4.0);
      const QJsonValue style = o.value("style");
      if (!style.isUndefined() &&
          (!style.isString() || (style.toString() != "solid" && style.toString() != "dashed" &&
                                 style.toString() != "dotted")))
        return err(e, QStringLiteral("layout: style must be solid|dashed|dotted"));
      line.style = style.toString(QStringLiteral("solid")).toStdString();
      const QJsonValue locked = o.value("locked");
      if (!locked.isUndefined() && !locked.isBool())
        return err(e, QStringLiteral("layout: locked must be a boolean"));
      line.locked = locked.toBool(false);
      const QJsonValue fill = o.value("fillColor");
      if (!fill.isUndefined() && !fill.isString())
        return err(e, QStringLiteral("layout: fillColor must be a string"));
      line.fillColor = fill.toString(QStringLiteral("transparent")).toStdString();
      return true;
    }

    bool parseLayout(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "lines"}); !k.isEmpty())
        return err(e, QStringLiteral("layout: unknown field \"%1\"").arg(k));
      if (!o.value("lines").isArray())
        return err(e, QStringLiteral("layout: \"lines\" must be an array"));
      const QJsonArray arr = o.value("lines").toArray();
      if (arr.size() > kMaxLayoutLines)
        return err(e, QStringLiteral("layout: too many lines (max %1)").arg(kMaxLayoutLines));
      for (const QJsonValue& lv : arr) {
        core::Line line;
        if (!parseLine(lv, line, e)) return false;
        a.lines.push_back(std::move(line));
      }
      return true;
    }

    bool parseFormula(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "axis", "expr", "enabled"}); !k.isEmpty())
        return err(e, QStringLiteral("formula: unknown field \"%1\"").arg(k));
      // §2: `enabled` is a bool ALONE (false switches formulas OFF, restoring
      // identity) — never combined with axis/expr.
      if (o.contains("enabled")) {
        if (o.contains("axis") || o.contains("expr"))
          return err(e, QStringLiteral(
                            "formula: \"enabled\" stands alone (no axis/expr beside it)"));
        if (!o.value("enabled").isBool())
          return err(e, QStringLiteral("formula: \"enabled\" must be a boolean"));
        a.formulaEnabled = o.value("enabled").toBool() ? 1 : 0;
        return true;
      }
      const QString axis = o.value("axis").toString();
      if (axis != "x" && axis != "y")
        return err(e, QStringLiteral("formula: axis must be \"x\" or \"y\""));
      a.axis = axis.at(0);
      if (!o.value("expr").isString())
        return err(e, QStringLiteral("formula: \"expr\" must be a string"));
      // §2: an empty expr CLEARS that axis (identity); non-empty must pass the
      // charset check (and the core engine again at execution time).
      a.expr = o.value("expr").toString().trimmed();
      if (!a.expr.isEmpty() && !isFormulaExpr(a.expr, a.axis))
        return err(e, QStringLiteral("formula: invalid expression"));
      return true;
    }

    // Shared by page/blank: a centimetre dim 0.1..500 (contract §2).
    bool asCmDim(const QJsonValue& v, double& out) {
      if (!v.isDouble()) return false;
      const double d = v.toDouble();
      if (d < 0.1 || d > 500.0) return false;
      out = d;
      return true;
    }

    bool parsePage(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "format", "width", "height"}); !k.isEmpty())
        return err(e, QStringLiteral("page: unknown field \"%1\"").arg(k));
      // §2: `format` OR `width`+`height` — exactly one of the two forms.
      const bool hasFormat = o.contains("format");
      const bool hasDims = o.contains("width") || o.contains("height");
      if (hasFormat == hasDims)
        return err(e, QStringLiteral(
                          "page: give exactly one of \"format\" / \"width\"+\"height\""));
      if (hasFormat) {
        a.format = o.value("format").toString();
        if (!isPageFormat(a.format))
          return err(e, QStringLiteral("page: format must be a0–a10 / b0–b10 / c0–c10"));
        return true;
      }
      if (!asCmDim(o.value("width"), a.widthCm) || !asCmDim(o.value("height"), a.heightCm))
        return err(e, QStringLiteral(
                          "page: width/height must both be centimetre numbers 0.1..500"));
      return true;
    }

    bool parseBlank(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "color", "format", "width", "height"});
          !k.isEmpty())
        return err(e, QStringLiteral("blank: unknown field \"%1\"").arg(k));
      if (!o.value("color").isString())
        return err(e, QStringLiteral("blank: \"color\" must be a string"));
      a.color = o.value("color").toString();
      if (!isColorNameOrHex(a.color))
        return err(e, QStringLiteral("blank: color must be \"#rrggbb\" or a CSS colour name"));
      if (o.contains("format")) {
        if (!o.value("format").isString() || !isPageFormat(o.value("format").toString()))
          return err(e, QStringLiteral("blank: format must be a0–a10 / b0–b10 / c0–c10"));
        a.format = o.value("format").toString();
      }
      // §2: optional cm dims — both or neither; they override `format`.
      const bool hasW = o.contains("width");
      const bool hasH = o.contains("height");
      if (hasW != hasH)
        return err(e, QStringLiteral("blank: width and height come together"));
      if (hasW &&
          (!asCmDim(o.value("width"), a.widthCm) || !asCmDim(o.value("height"), a.heightCm)))
        return err(e, QStringLiteral(
                          "blank: width/height must both be centimetre numbers 0.1..500"));
      return true;
    }

    // §2 undo / redo: an optional steps count (1..20, default 1).
    bool parseUndoRedo(const QJsonObject& o, Action& a, const char* opName, QString* e) {
      if (const QString k = unknownKey(o, {"op", "steps"}); !k.isEmpty())
        return err(e, QStringLiteral("%1: unknown field \"%2\"")
                          .arg(QLatin1String(opName), k));
      a.steps = 1;
      if (o.contains("steps")) {
        if (!asInt(o.value("steps"), a.steps) || a.steps < 1 || a.steps > 20)
          return err(e, QStringLiteral("%1: steps must be an integer 1..20")
                            .arg(QLatin1String(opName)));
      }
      return true;
    }

    bool parseFrame(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "index", "indices"}); !k.isEmpty())
        return err(e, QStringLiteral("frame: unknown field \"%1\"").arg(k));
      const bool hasIndex = o.contains("index");
      const bool hasIndices = o.contains("indices");
      if (hasIndex == hasIndices)
        return err(e, QStringLiteral("frame: give exactly one of \"index\" / \"indices\""));
      if (hasIndex) {
        int idx = 0;
        if (!asInt(o.value("index"), idx) || idx < 0)
          return err(e, QStringLiteral("frame: index must be an integer ≥ 0"));
        a.indices.push_back(idx);
        return true;
      }
      if (!o.value("indices").isArray())
        return err(e, QStringLiteral("frame: \"indices\" must be an array"));
      const QJsonArray arr = o.value("indices").toArray();
      if (arr.isEmpty() || arr.size() > kMaxFrameIndices)
        return err(e, QStringLiteral("frame: 1..%1 indices").arg(kMaxFrameIndices));
      for (const QJsonValue& v : arr) {
        int idx = 0;
        if (!asInt(v, idx) || idx < 0)
          return err(e, QStringLiteral("frame: indices must be integers ≥ 0"));
        a.indices.push_back(idx);
      }
      return true;
    }

    // ── §10 editor-settings ops (GUI editors only) ───────────────────────────

    bool parseTheme(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "mode"}); !k.isEmpty())
        return err(e, QStringLiteral("theme: unknown field \"%1\"").arg(k));
      a.mode = o.value("mode").toString();
      if (a.mode != "light" && a.mode != "dark")
        return err(e, QStringLiteral("theme: mode must be \"light\" or \"dark\""));
      return true;
    }

    bool parseAccent(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "color", "preset"}); !k.isEmpty())
        return err(e, QStringLiteral("accent: unknown field \"%1\"").arg(k));
      // §10: hex OR a named preset — exactly one form. Unknown preset NAMES are
      // the executor's note+skip; the shape is validated here.
      const bool hasColor = o.contains("color");
      const bool hasPreset = o.contains("preset");
      if (hasColor == hasPreset)
        return err(e, QStringLiteral(
                          "accent: give exactly one of \"color\" / \"preset\""));
      if (hasPreset) {
        if (!o.value("preset").isString())
          return err(e, QStringLiteral("accent: \"preset\" must be a string"));
        a.preset = o.value("preset").toString().trimmed();
        if (a.preset.isEmpty() || a.preset.size() > 40)
          return err(e, QStringLiteral("accent: bad preset name"));
        return true;
      }
      a.color = o.value("color").toString();
      if (!isHexColor(a.color))
        return err(e, QStringLiteral("accent: color must be \"#rrggbb\""));
      return true;
    }

    bool parseLineStyle(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "color", "thickness", "pointSize",
                                           "style", "pointColor", "drawMode",
                                           "fillColor"});
          !k.isEmpty())
        return err(e, QStringLiteral("lineStyle: unknown field \"%1\"").arg(k));
      bool any = false;
      if (o.contains("color")) {
        if (!o.value("color").isString())
          return err(e, QStringLiteral("lineStyle: color must be a string"));
        a.color = o.value("color").toString();
        if (!isColorNameOrHex(a.color))
          return err(e, QStringLiteral(
                            "lineStyle: color must be \"#rrggbb\" or a CSS colour name"));
        any = true;
      }
      if (o.contains("thickness")) {
        if (!asInt(o.value("thickness"), a.thickness) || a.thickness < 1 ||
            a.thickness > 20)
          return err(e, QStringLiteral("lineStyle: thickness must be an integer 1..20"));
        any = true;
      }
      if (o.contains("pointSize")) {
        if (!asInt(o.value("pointSize"), a.pointSize) || a.pointSize < 1 ||
            a.pointSize > 30)
          return err(e, QStringLiteral("lineStyle: pointSize must be an integer 1..30"));
        any = true;
      }
      if (o.contains("style")) {
        a.style = o.value("style").toString();
        if (a.style != "solid" && a.style != "dashed" && a.style != "dotted")
          return err(e, QStringLiteral("lineStyle: style must be solid|dashed|dotted"));
        any = true;
      }
      // §10 widening: pointColor ("" = follow the stroke — meaningful, so a
      // presence flag rides along), drawMode line|rect, fillColor
      // "#rrggbb"|"transparent" (a browser control — the desktop notes+skips it
      // at execution time, never at parse time).
      if (o.contains("pointColor")) {
        if (!o.value("pointColor").isString())
          return err(e, QStringLiteral("lineStyle: pointColor must be a string"));
        a.pointColor = o.value("pointColor").toString();
        if (!a.pointColor.isEmpty() && !isHexColor(a.pointColor))
          return err(e, QStringLiteral(
                            "lineStyle: pointColor must be \"#rrggbb\" or \"\""));
        a.pointColorSet = true;
        any = true;
      }
      if (o.contains("drawMode")) {
        a.drawMode = o.value("drawMode").toString();
        if (a.drawMode != "line" && a.drawMode != "rect")
          return err(e, QStringLiteral("lineStyle: drawMode must be \"line\" or \"rect\""));
        any = true;
      }
      if (o.contains("fillColor")) {
        a.fillColor = o.value("fillColor").toString();
        if (a.fillColor != "transparent" && !isHexColor(a.fillColor))
          return err(e, QStringLiteral(
                            "lineStyle: fillColor must be \"#rrggbb\" or \"transparent\""));
        any = true;
      }
      if (!any) return err(e, QStringLiteral("lineStyle: needs at least one field"));
      return true;
    }

    bool parseUnits(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "value"}); !k.isEmpty())
        return err(e, QStringLiteral("units: unknown field \"%1\"").arg(k));
      a.value = o.value("value").toString();
      if (a.value != "cm" && a.value != "in")
        return err(e, QStringLiteral("units: value must be \"cm\" or \"in\""));
      return true;
    }

    bool parseClear(const QJsonObject& o, QString* e) {
      if (const QString k = unknownKey(o, {"op"}); !k.isEmpty())
        return err(e, QStringLiteral("clear: unknown field \"%1\"").arg(k));
      return true;
    }

    // §10 copy: an optional `what` ("image" default | "layout") — the
    // working-image / drawn-lines requirements are the executor's (a note +
    // skip, never a parse failure).
    bool parseCopy(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "what"}); !k.isEmpty())
        return err(e, QStringLiteral("copy: unknown field \"%1\"").arg(k));
      if (o.contains("what")) {
        a.what = o.value("what").toString();
        if (a.what != "image" && a.what != "layout")
          return err(e, QStringLiteral("copy: what must be \"image\" or \"layout\""));
      }
      return true;
    }

    bool parseView(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "points", "lines"}); !k.isEmpty())
        return err(e, QStringLiteral("view: unknown field \"%1\"").arg(k));
      bool any = false;
      if (o.contains("points")) {
        if (!o.value("points").isBool())
          return err(e, QStringLiteral("view: points must be a boolean"));
        a.viewPoints = o.value("points").toBool() ? 1 : 0;
        any = true;
      }
      if (o.contains("lines")) {
        if (!o.value("lines").isBool())
          return err(e, QStringLiteral("view: lines must be a boolean"));
        a.viewLines = o.value("lines").toBool() ? 1 : 0;
        any = true;
      }
      if (!any) return err(e, QStringLiteral("view: needs at least one field"));
      return true;
    }

    // connect / disconnect share the shape: a non-empty server reference. The
    // reference is resolved against the SAVED/live stores at execution time —
    // the model can never introduce a new host, and plans never carry tokens.
    bool parseServerRef(const QJsonObject& o, Action& a, const char* opName, QString* e) {
      if (const QString k = unknownKey(o, {"op", "server"}); !k.isEmpty())
        return err(e, QStringLiteral("%1: unknown field \"%2\"")
                          .arg(QLatin1String(opName), k));
      if (!o.value("server").isString())
        return err(e, QStringLiteral("%1: \"server\" must be a string")
                          .arg(QLatin1String(opName)));
      a.server = o.value("server").toString().trimmed();
      if (a.server.isEmpty() || a.server.size() > kMaxSpecChars)
        return err(e, QStringLiteral("%1: bad server reference").arg(QLatin1String(opName)));
      return true;
    }

    // §10 openUrl: an http(s) URL + optional incognito. The USER-ECHO guard (the
    // URL must appear in the user's own messages) is the executor's job — the
    // parser has no conversation context.
    bool parseOpenUrl(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "url", "incognito"}); !k.isEmpty())
        return err(e, QStringLiteral("openUrl: unknown field \"%1\"").arg(k));
      if (!o.value("url").isString())
        return err(e, QStringLiteral("openUrl: \"url\" must be a string"));
      a.url = o.value("url").toString().trimmed();
      static const QRegularExpression kHttpUrl(
          QStringLiteral("^https?://\\S+$"), QRegularExpression::CaseInsensitiveOption);
      if (a.url.isEmpty() || a.url.size() > kMaxSpecChars || !kHttpUrl.match(a.url).hasMatch())
        return err(e, QStringLiteral("openUrl: \"url\" must be an http(s) URL"));
      if (o.contains("incognito")) {
        if (!o.value("incognito").isBool())
          return err(e, QStringLiteral("openUrl: \"incognito\" must be a boolean"));
        a.incognito = o.value("incognito").toBool();
      }
      return true;
    }

    // Anything with a "scheme://" prefix is a URL, and a local-path field must never
    // carry one — openUrl is the op for those, with its own network guard.
    bool hasUrlScheme(const QString& path) {
      static const QRegularExpression kScheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
      return kScheme.match(path).hasMatch();
    }

    // The read scope: only the formats this app itself opens, decided by extension so the
    // model can never hand us an arbitrary file to slurp. Mirrors the cli's understoodPath.
    bool isOpenableFile(const QString& path) {
      static const QStringList kExts = {
          QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
          QStringLiteral("bmp"),  QStringLiteral("tga"),  QStringLiteral("gif"),
          QStringLiteral("webp"), QStringLiteral("mp4"),  QStringLiteral("mov"),
          QStringLiteral("m4v"),  QStringLiteral("avi"),  QStringLiteral("mkv"),
          QStringLiteral("webm"), QStringLiteral("json"), QStringLiteral("stencil")};
      const int dot = path.lastIndexOf(QLatin1Char('.'));
      if (dot < 0) return false;
      const int slash = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
      if (dot < slash) return false;  // the dot is in a directory name
      return kExts.contains(path.mid(dot + 1).toLower());
    }

    // §10 openFile: a LOCAL path in a format this app opens. Whether the USER wrote
    // it is checked in the executor, exactly like openUrl's echo guard.
    bool parseOpenFile(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "path"}); !k.isEmpty())
        return err(e, QStringLiteral("openFile: unknown field \"%1\"").arg(k));
      if (!o.value("path").isString())
        return err(e, QStringLiteral("openFile: \"path\" must be a local file path"));
      a.path = o.value("path").toString().trimmed();
      if (a.path.isEmpty() || a.path.size() > kMaxPathChars || hasUrlScheme(a.path))
        return err(e, QStringLiteral("openFile: \"path\" must be a local file path"));
      if (!isOpenableFile(a.path))
        return err(e, QStringLiteral("openFile: \"%1\" is not an image, video, .json layout "
                                     "or .stencil project")
                          .arg(a.path));
      return true;
    }

    // Shared name shape for the project ops (removeProject/openProject 1..120,
    // renameProject 1..80): non-empty after trimming, bounded.
    bool parseProjectName(const QJsonObject& o, Action& a, const char* opName, int maxLen,
                          QString* e) {
      const QJsonValue v = o.value("name");
      if (!v.isString() || v.toString().size() > maxLen || v.toString().trimmed().isEmpty())
        return err(e, QStringLiteral("%1: \"name\" must be a non-empty "
                                     "string of at most %2 characters")
                          .arg(QLatin1String(opName))
                          .arg(maxLen));
      a.name = v.toString().trimmed();
      return true;
    }

    // §10 removeProject: one saved LOCAL project by name, OR `current: true`
    // (the active project) — exactly one form. Resolution — exact, else unique
    // case-insensitive prefix — and the confirm are the target's.
    bool parseRemoveProject(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "name", "current"}); !k.isEmpty())
        return err(e, QStringLiteral("removeProject: unknown field \"%1\"").arg(k));
      const bool hasName = o.contains("name");
      const bool hasCurrent = o.contains("current");
      if (hasName == hasCurrent)
        return err(e, QStringLiteral(
                          "removeProject: give exactly one of \"name\" / \"current\""));
      if (hasCurrent) {
        if (!o.value("current").isBool() || !o.value("current").toBool())
          return err(e, QStringLiteral("removeProject: \"current\" must be true"));
        a.current = true;
        return true;
      }
      return parseProjectName(o, a, "removeProject", 120, e);
    }

    // ── §10 new editor rows: compare/zoom/renameProject/projectColor/
    //    blankColor/openProject/incognito ──

    bool parseCompare(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "mode", "split"}); !k.isEmpty())
        return err(e, QStringLiteral("compare: unknown field \"%1\"").arg(k));
      a.mode = o.value("mode").toString();
      static const QStringList modes = {"none", "original", "vertical", "horizontal"};
      if (!modes.contains(a.mode))
        return err(e, QStringLiteral(
                          "compare: mode must be none|original|vertical|horizontal"));
      if (o.contains("split")) {
        // The divider fraction belongs to the SPLIT modes only (contract §10).
        // Models echo the previous divider back with "none"/"original" — an
        // ignored field there, never a failed plan (the mode is the intent).
        if (a.mode != "vertical" && a.mode != "horizontal") return true;
        if (!o.value("split").isDouble())
          return err(e, QStringLiteral("compare: split must be a number"));
        a.split = o.value("split").toDouble();
        if (a.split < 0.02 || a.split > 0.98)
          return err(e, QStringLiteral("compare: split must be 0.02..0.98"));
      }
      return true;
    }

    bool parseZoom(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "percent", "fit"}); !k.isEmpty())
        return err(e, QStringLiteral("zoom: unknown field \"%1\"").arg(k));
      const bool hasPercent = o.contains("percent");
      const bool hasFit = o.contains("fit");
      if (hasPercent == hasFit)
        return err(e, QStringLiteral("zoom: give exactly one of \"percent\" / \"fit\""));
      if (hasFit) {
        if (!o.value("fit").isBool() || !o.value("fit").toBool())
          return err(e, QStringLiteral("zoom: \"fit\" must be true"));
        a.fit = true;
        return true;
      }
      if (!asInt(o.value("percent"), a.percent) || a.percent < 5 || a.percent > 3200)
        return err(e, QStringLiteral("zoom: percent must be an integer 5..3200"));
      return true;
    }

    bool parseRenameProject(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "name"}); !k.isEmpty())
        return err(e, QStringLiteral("renameProject: unknown field \"%1\"").arg(k));
      return parseProjectName(o, a, "renameProject", 80, e);
    }

    bool parseProjectColor(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "color"}); !k.isEmpty())
        return err(e, QStringLiteral("projectColor: unknown field \"%1\"").arg(k));
      if (!o.value("color").isString())
        return err(e, QStringLiteral("projectColor: \"color\" must be a string"));
      // "" is the explicit clear (restore the theme accent) — contract §10.
      a.color = o.value("color").toString();
      if (!a.color.isEmpty() && !isHexColor(a.color))
        return err(e, QStringLiteral("projectColor: color must be \"#rrggbb\" or \"\""));
      return true;
    }

    bool parseBlankColor(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "color"}); !k.isEmpty())
        return err(e, QStringLiteral("blankColor: unknown field \"%1\"").arg(k));
      if (!o.value("color").isString())
        return err(e, QStringLiteral("blankColor: \"color\" must be a string"));
      a.color = o.value("color").toString();
      if (!isColorNameOrHex(a.color))
        return err(e, QStringLiteral(
                          "blankColor: color must be \"#rrggbb\" or a CSS colour name"));
      return true;
    }

    // §10 openProject: a saved project by name, OR `last: true` — the most recently
    // edited one ("the last project I worked on"). Exactly one form, removeProject's
    // `current: true` shape; the resolution is the target's.
    bool parseOpenProject(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "name", "last"}); !k.isEmpty())
        return err(e, QStringLiteral("openProject: unknown field \"%1\"").arg(k));
      const bool hasName = o.contains("name");
      const bool hasLast = o.contains("last");
      if (hasName == hasLast)
        return err(e, QStringLiteral("openProject: give exactly one of \"name\" / \"last\""));
      if (hasLast) {
        if (!o.value("last").isBool() || !o.value("last").toBool())
          return err(e, QStringLiteral("openProject: \"last\" must be true"));
        a.current = true;   // "the latest one" rides removeProject's own presence flag
        return true;
      }
      return parseProjectName(o, a, "openProject", 120, e);
    }

    bool parseIncognito(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "on"}); !k.isEmpty())
        return err(e, QStringLiteral("incognito: unknown field \"%1\"").arg(k));
      if (!o.value("on").isBool())
        return err(e, QStringLiteral("incognito: \"on\" must be a boolean"));
      a.incognito = o.value("on").toBool();
      return true;
    }

    // §10 chatPanel: where the assistant panel itself sits. At least one of
    // "open"/"dock"; a dock with no open opens it too (browser opPlan.js parity).
    bool parseChatPanel(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "open", "dock"}); !k.isEmpty())
        return err(e, QStringLiteral("chatPanel: unknown field \"%1\"").arg(k));
      const bool hasOpen = o.contains("open");
      const bool hasDock = o.contains("dock");
      if (!hasOpen && !hasDock)
        return err(e, QStringLiteral("chatPanel: needs \"open\" and/or \"dock\""));
      if (hasOpen) {
        if (!o.value("open").isBool())
          return err(e, QStringLiteral("chatPanel: \"open\" must be a boolean"));
        a.chatOpen = o.value("open").toBool() ? 1 : 0;
      }
      if (hasDock) {
        const QString d = o.value("dock").toString();
        static const QStringList kDocks{QStringLiteral("left"), QStringLiteral("right"),
                                        QStringLiteral("top"), QStringLiteral("bottom"),
                                        QStringLiteral("float")};
        if (!kDocks.contains(d))
          return err(e, QStringLiteral("chatPanel: \"dock\" must be one of %1")
                            .arg(kDocks.join(QStringLiteral(", "))));
        a.dock = d;
      }
      return true;
    }

    // §10 dialog: one of the editor's own windows, or close:true for the open one —
    // exactly one form. The assistant's provider settings are never among the names
    // (§13 forbidden); the browser opPlan.js list is the canon.
    bool parseDialog(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "name", "close"}); !k.isEmpty())
        return err(e, QStringLiteral("dialog: unknown field \"%1\"").arg(k));
      const bool hasName = o.contains("name");
      const bool hasClose = o.contains("close");
      if (hasName == hasClose)
        return err(e, QStringLiteral("dialog: give exactly one of \"name\" / \"close\""));
      if (hasClose) {
        if (!o.value("close").isBool() || !o.value("close").toBool())
          return err(e, QStringLiteral("dialog: \"close\" must be true"));
        a.current = true;   // "close what is open" rides the shared presence flag
        return true;
      }
      static const QStringList kNames{QStringLiteral("projects"), QStringLiteral("servers"),
                                      QStringLiteral("shortcuts"), QStringLiteral("visuals"),
                                      QStringLiteral("help")};
      const QString n = o.value("name").toString();
      if (!kNames.contains(n))
        return err(e, QStringLiteral("dialog: \"name\" must be one of %1")
                          .arg(kNames.join(QStringLiteral(", "))));
      a.dialog = n;
      return true;
    }

    // §10 clearProjects: fieldless like clear; the empty-store note and the
    // confirm are the target's.
    // §10 clearProjects: every saved LOCAL project, or every one but the open project
    // (`keepCurrent: true` — "delete the others"). Present-and-true or absent, like
    // removeProject's `current`; the confirm and the count are the target's.
    bool parseClearProjects(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "keepCurrent"}); !k.isEmpty())
        return err(e, QStringLiteral("clearProjects: unknown field \"%1\"").arg(k));
      if (o.contains("keepCurrent")) {
        if (!o.value("keepCurrent").isBool() || !o.value("keepCurrent").toBool())
          return err(e, QStringLiteral("clearProjects: \"keepCurrent\" must be true"));
        a.current = true;   // the shared "the open one" presence flag
      }
      return true;
    }

    // §10 clearChat: fieldless like clearProjects; the confirm is the surface's,
    // deferred to the end of the turn.
    bool parseClearChat(const QJsonObject& o, QString* e) {
      if (const QString k = unknownKey(o, {"op"}); !k.isEmpty())
        return err(e, QStringLiteral("clearChat: unknown field \"%1\"").arg(k));
      return true;
    }

    // §2.1 image: which attachment of THIS turn becomes the working image.
    // 1-based; 0, negatives and non-integers are not an attachment.
    bool parseImage(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "index"}); !k.isEmpty())
        return err(e, QStringLiteral("image: unknown field \"%1\"").arg(k));
      if (!asInt(o.value("index"), a.index) || a.index < 1)
        return err(e, QStringLiteral("image: \"index\" must be an integer >= 1"));
      return true;
    }

    // §2.1 save: an optional project name (≤ 120 chars); absent = derived at
    // execution time from the attachment being worked on.
    bool parseSave(const QJsonObject& o, Action& a, QString* e) {
      if (const QString k = unknownKey(o, {"op", "name", "path"}); !k.isEmpty())
        return err(e, QStringLiteral("save: unknown field \"%1\"").arg(k));
      // §10: an optional destination the USER named — a folder or a file name, never a URL.
      // The echo guard runs in the executor; here only the shape is checked.
      if (const QJsonValue d = o.value("path"); !d.isUndefined() && !d.isNull()) {
        if (!d.isString() || d.toString().size() > kMaxPathChars)
          return err(e, QStringLiteral("save: \"path\" must be a string of at most %1 characters")
                            .arg(kMaxPathChars));
        a.path = d.toString().trimmed();
        if (hasUrlScheme(a.path))
          return err(e, QStringLiteral("save: \"path\" is a local path, not a URL"));
      }
      const QJsonValue v = o.value("name");
      if (v.isUndefined() || v.isNull()) return true;
      if (!v.isString() || v.toString().size() > 120)
        return err(e, QStringLiteral(
                          "save: \"name\" must be a string of at most 120 characters"));
      a.name = v.toString();
      return true;
    }

    // One action list (top-level or a variant's). Unknown op ⇒ skip + warning;
    // known op with bad params ⇒ fail (the caller fails the whole plan);
    // a top-level-only op inside a variant/preview ⇒ *scopeDrop = why, and the
    // CALLER drops that variant (or that option's preview) with a warning
    // instead of failing the plan (§1's one exception).
    bool parseActions(const QJsonValue& v, QVector<Action>& out, QStringList& warnings,
                      bool inVariant, QString* e, QString* scopeDrop = nullptr) {
      if (v.isUndefined() || v.isNull()) return true;  // absent = empty
      if (!v.isArray()) return err(e, QStringLiteral("\"actions\" must be an array"));
      for (const QJsonValue& av : v.toArray()) {
        if (!av.isObject()) return err(e, QStringLiteral("action must be an object"));
        const QJsonObject o = av.toObject();
        if (!o.value("op").isString())
          return err(e, QStringLiteral("action has no \"op\""));
        const QString op = o.value("op").toString();
        Action a;
        bool ok = true;
        if (op == "crop") { a.op = OpKind::Crop; ok = parseCrop(o, a, e); }
        else if (op == "rotate") { a.op = OpKind::Rotate; ok = parseRotate(o, a, e); }
        else if (op == "filter") { a.op = OpKind::Filter; ok = parseFilter(o, a, e); }
        else if (op == "layout") { a.op = OpKind::Layout; ok = parseLayout(o, a, e); }
        else if (op == "formula") { a.op = OpKind::Formula; ok = parseFormula(o, a, e); }
        else if (op == "page") { a.op = OpKind::Page; ok = parsePage(o, a, e); }
        else if (op == "blank") { a.op = OpKind::Blank; ok = parseBlank(o, a, e); }
        else if (op == "frame") { a.op = OpKind::Frame; ok = parseFrame(o, a, e); }
        else if (op == "theme") { a.op = OpKind::Theme; ok = parseTheme(o, a, e); }
        else if (op == "accent") { a.op = OpKind::Accent; ok = parseAccent(o, a, e); }
        else if (op == "lineStyle") { a.op = OpKind::LineStyle; ok = parseLineStyle(o, a, e); }
        else if (op == "units") { a.op = OpKind::Units; ok = parseUnits(o, a, e); }
        else if (op == "view") { a.op = OpKind::View; ok = parseView(o, a, e); }
        else if (op == "clear") { a.op = OpKind::Clear; ok = parseClear(o, e); }
        else if (op == "copy") { a.op = OpKind::Copy; ok = parseCopy(o, a, e); }
        else if (op == "openUrl") { a.op = OpKind::OpenUrl; ok = parseOpenUrl(o, a, e); }
        else if (op == "openFile") { a.op = OpKind::OpenFile; ok = parseOpenFile(o, a, e); }
        else if (op == "connect") { a.op = OpKind::Connect; ok = parseServerRef(o, a, "connect", e); }
        else if (op == "disconnect") { a.op = OpKind::Disconnect; ok = parseServerRef(o, a, "disconnect", e); }
        else if (op == "removeProject") { a.op = OpKind::RemoveProject; ok = parseRemoveProject(o, a, e); }
        else if (op == "clearProjects") { a.op = OpKind::ClearProjects; ok = parseClearProjects(o, a, e); }
        else if (op == "compare") { a.op = OpKind::Compare; ok = parseCompare(o, a, e); }
        else if (op == "zoom") { a.op = OpKind::Zoom; ok = parseZoom(o, a, e); }
        else if (op == "renameProject") { a.op = OpKind::RenameProject; ok = parseRenameProject(o, a, e); }
        else if (op == "projectColor") { a.op = OpKind::ProjectColor; ok = parseProjectColor(o, a, e); }
        else if (op == "blankColor") { a.op = OpKind::BlankColor; ok = parseBlankColor(o, a, e); }
        else if (op == "openProject") { a.op = OpKind::OpenProject; ok = parseOpenProject(o, a, e); }
        else if (op == "incognito") { a.op = OpKind::Incognito; ok = parseIncognito(o, a, e); }
        else if (op == "chatPanel") { a.op = OpKind::ChatPanel; ok = parseChatPanel(o, a, e); }
        else if (op == "dialog") { a.op = OpKind::Dialog; ok = parseDialog(o, a, e); }
        else if (op == "clearChat") { a.op = OpKind::ClearChat; ok = parseClearChat(o, e); }
        else if (op == "undo") { a.op = OpKind::Undo; ok = parseUndoRedo(o, a, "undo", e); }
        else if (op == "redo") { a.op = OpKind::Redo; ok = parseUndoRedo(o, a, "redo", e); }
        else if (op == "image") { a.op = OpKind::Image; ok = parseImage(o, a, e); }
        else if (op == "save") { a.op = OpKind::Save; ok = parseSave(o, a, e); }
        else {
          // Forward compatibility: an unknown op is dropped with a warning.
          warnings << QStringLiteral("Skipped unknown op \"%1\".").arg(op);
          continue;
        }
        if (!ok) return false;
        if (inVariant && isTopLevelOnlyOp(a.op)) {
          QString why;
          if (isHistoryOp(a.op)) {
            // §2 undo/redo: their own wording — a sandboxed variant/preview
            // render writes history-invisible state.
            why = QStringLiteral(
                      "\"%1\" is a top-level action only — a sandboxed variant/preview "
                      "has no edit history")
                      .arg(op);
          } else if (!isEditorSettingsOp(a.op)) {   // §2.1 image/save
            why = QStringLiteral("\"%1\" is a top-level action only (§2.1)").arg(op);
          } else {
            why = QStringLiteral("\"%1\" is an editor-settings op, not an image edit").arg(op);
            if (a.op == OpKind::OpenUrl)
              why += QStringLiteral(" — open the URL as a top-level action; picking images "
                                    "off a web page is the browser extension assistant's job");
          }
          // §1: costs this variant/preview its place, never the whole plan.
          if (scopeDrop) { *scopeDrop = why; return false; }
          return err(e, why);
        }
        out.push_back(std::move(a));
      }
      if (out.size() > kMaxActions)
        return err(e, QStringLiteral("too many actions (max %1)").arg(kMaxActions));
      return true;
    }

  }  // namespace

  // ── §11 interactive replies (`ask`) ──
  // Validated as strictly as an action: a card nobody can answer (no options, one option, six
  // options, an option that is both a render and a reference) fails the whole plan rather than
  // reaching the user as a broken prompt.
  static bool parseAskImage(const QJsonObject& img, int index, AskOption& out, QStringList& warnings, QString* e) {
    if (const QString k = unknownKey(img, {"url", "projectId", "scanIndex"}); !k.isEmpty())
      return err(e, QStringLiteral("ask option %1 \"image\" has an unknown field \"%2\"").arg(index).arg(k));
    int given = 0;
    QString only;
    for (const QString& k : {QStringLiteral("url"), QStringLiteral("projectId"), QStringLiteral("scanIndex")}) {
      const QJsonValue v = img.value(k);
      if (!v.isUndefined() && !v.isNull()) {
        ++given;
        only = k;
      }
    }
    if (given != 1)
      return err(e, QStringLiteral("ask option %1 \"image\" needs exactly one of url, projectId, scanIndex").arg(index));
    if (only == "scanIndex") {
      // The extension's reference (§8): meaningless in the editor, so the option stays pictureless.
      const QJsonValue v = img.value(only);
      if (!v.isDouble() || v.toDouble() < 0 || v.toDouble() != std::floor(v.toDouble()))
        return err(e, QStringLiteral("ask option %1 \"image.scanIndex\" must be an integer >= 0").arg(index));
      warnings << QStringLiteral("option %1 names a page-scan image, which this editor cannot show").arg(index);
      return true;
    }
    const QJsonValue v = img.value(only);
    if (!v.isString() || v.toString().trimmed().isEmpty())
      return err(e, QStringLiteral("ask option %1 \"image.%2\" must be a non-empty string").arg(index).arg(only));
    const QString text = v.toString().trimmed();
    if (only == "projectId") {
      out.projectId = text;
      return true;
    }
    // Only http(s) is fetchable — a data:/file: "image" is never followed.
    const QUrl url(text);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https"))
      return err(e, QStringLiteral("ask option %1 \"image.url\" must be an http(s) URL").arg(index));
    out.imageUrl = text;
    return true;
  }

  static bool parseAsk(const QJsonValue& value, AskCard& out, QStringList& warnings, QString* e) {
    if (value.isUndefined() || value.isNull()) return true;   // no card is the norm
    if (!value.isObject()) return err(e, QStringLiteral("\"ask\" must be an object"));
    const QJsonObject ask = value.toObject();
    if (const QString k = unknownKey(ask, {"question", "mode", "options", "allowCustom", "customLabel"});
        !k.isEmpty())
      return err(e, QStringLiteral("\"ask\" has an unknown field \"%1\"").arg(k));
    const QJsonValue q = ask.value("question");
    if (!q.isString() || q.toString().trimmed().isEmpty())
      return err(e, QStringLiteral("\"ask.question\" must be a non-empty string"));
    if (q.toString().size() > kMaxAskQuestion)
      return err(e, QStringLiteral("\"ask.question\" is longer than %1 characters").arg(kMaxAskQuestion));
    out.question = q.toString().trimmed();

    const QJsonValue mode = ask.value("mode");
    if (!mode.isUndefined() && !mode.isNull()) {
      const QString m = mode.isString() ? mode.toString() : QString();
      if (m == "multi") {
        out.multi = true;
      } else if (m != "single") {
        return err(e, QStringLiteral("\"ask.mode\" must be \"single\" or \"multi\""));
      }
    }
    const QJsonValue custom = ask.value("allowCustom");
    if (!custom.isUndefined() && !custom.isNull()) {
      if (!custom.isBool())
        return err(e, QStringLiteral("\"ask.allowCustom\" must be a boolean"));
      out.allowCustom = custom.toBool();
    }
    out.customLabel = QStringLiteral("Something else…");
    const QJsonValue label = ask.value("customLabel");
    if (!label.isUndefined() && !label.isNull()) {
      if (!label.isString() || label.toString().trimmed().isEmpty())
        return err(e, QStringLiteral("\"ask.customLabel\" must be a non-empty string"));
      if (label.toString().size() > kMaxAskLabel)
        return err(e, QStringLiteral("\"ask.customLabel\" is longer than %1 characters").arg(kMaxAskLabel));
      out.customLabel = label.toString().trimmed();
    }

    const QJsonValue opts = ask.value("options");
    if (!opts.isArray()) return err(e, QStringLiteral("\"ask.options\" must be an array"));
    const QJsonArray arr = opts.toArray();
    if (arr.size() < kMinAskOptions || arr.size() > kMaxAskOptions)
      return err(e, QStringLiteral("\"ask.options\" must hold %1..%2 options").arg(kMinAskOptions).arg(kMaxAskOptions));
    int index = 0;
    for (const QJsonValue& ov : arr) {
      ++index;
      if (!ov.isObject())
        return err(e, QStringLiteral("ask option %1 must be an object").arg(index));
      const QJsonObject oo = ov.toObject();
      if (const QString k = unknownKey(oo, {"label", "actions", "image"}); !k.isEmpty())
        return err(e, QStringLiteral("ask option %1 has an unknown field \"%2\"").arg(index).arg(k));
      const QJsonValue lv = oo.value("label");
      if (!lv.isString() || lv.toString().trimmed().isEmpty())
        return err(e, QStringLiteral("ask option %1 \"label\" must be a non-empty string").arg(index));
      if (lv.toString().size() > kMaxAskLabel)
        return err(e, QStringLiteral("ask option %1 \"label\" is longer than %2 characters").arg(index).arg(kMaxAskLabel));
      AskOption opt;
      opt.label = lv.toString().trimmed();
      const QJsonValue av = oo.value("actions");
      const QJsonValue iv = oo.value("image");
      const bool hasActions = !av.isUndefined() && !av.isNull();
      const bool hasImage = !iv.isUndefined() && !iv.isNull();
      if (hasActions && hasImage)
        return err(e, QStringLiteral("ask option %1 carries both \"actions\" and \"image\"").arg(index));
      if (hasActions) {
        // Preview actions are rendered, never executed — editor-settings ops make no sense
        // here, so they are parsed under the same rule that bans them inside variants.
        // §1/§11.2: a misplaced one costs the PREVIEW, not the plan — the option stays,
        // pictureless, and the preview's own warnings go with the render it never gets.
        QStringList previewWarnings;
        QString scopeDrop;
        if (!parseActions(av, opt.actions, previewWarnings, /*inVariant=*/true, e,
                          &scopeDrop)) {
          if (scopeDrop.isEmpty()) return false;
          opt.actions.clear();
          warnings << QStringLiteral(
                          "Dropped the preview for option %1 \"%2\" — %3; put it in the "
                          "plan's top-level actions. The option is still offered.")
                          .arg(index)
                          .arg(opt.label, scopeDrop);
        } else {
          warnings += previewWarnings;
        }
      }
      if (hasImage) {
        if (!iv.isObject())
          return err(e, QStringLiteral("ask option %1 \"image\" must be an object").arg(index));
        if (!parseAskImage(iv.toObject(), index, opt, warnings, e)) return false;
      }
      out.options.push_back(std::move(opt));
    }
    return true;
  }

  OpPlanResult parseOpPlan(const QString& text) {
    OpPlanResult r;
    QJsonObject obj;
    if (!extractFirstObject(stripFences(text), obj)) {
      // Chat-only turn: the raw text is the reply (not an error).
      r.ok = true;
      r.plan.chatOnly = true;
      r.plan.reply = text.trimmed();
      return r;
    }
    const QJsonValue reply = obj.value("reply");
    // §1 reply tolerance: models routinely omit the reply while planning valid
    // actions — substitute rather than lose the plan to a missing pleasantry.
    const bool replyOmitted = !reply.isString() || reply.toString().trimmed().isEmpty();
    if (!replyOmitted) r.plan.reply = reply.toString();
    QString e;
    if (!parseActions(obj.value("actions"), r.plan.actions, r.plan.warnings,
                      /*inVariant=*/false, &e)) {
      r.plan = {};
      r.error = e;
      return r;
    }
    const QJsonValue vars = obj.value("variants");
    if (!vars.isUndefined() && !vars.isNull()) {
      if (!vars.isArray()) {
        r.plan = {};
        r.error = QStringLiteral("\"variants\" must be an array");
        return r;
      }
      const QJsonArray arr = vars.toArray();
      if (arr.size() > kMaxVariants) {
        r.plan = {};
        r.error = QStringLiteral("too many variants (max %1)").arg(kMaxVariants);
        return r;
      }
      int vIndex = 0;
      for (const QJsonValue& vv : arr) {
        ++vIndex;
        if (!vv.isObject()) {
          r.plan = {};
          r.error = QStringLiteral("variant must be an object");
          return r;
        }
        const QJsonObject vo = vv.toObject();
        Variant var;
        var.label = vo.value("label").toString();  // non-string/absent → empty
        // §1: a top-level-only op in here drops THIS variant with a warning; the
        // rest of the plan runs. Its own warnings go with it.
        QStringList varWarnings;
        QString scopeDrop;
        if (!parseActions(vo.value("actions"), var.actions, varWarnings,
                          /*inVariant=*/true, &e, &scopeDrop)) {
          if (scopeDrop.isEmpty()) {
            r.plan = {};
            r.error = e;
            return r;
          }
          const QString who = var.label.trimmed().isEmpty()
                                  ? QStringLiteral("variant %1").arg(vIndex)
                                  : QStringLiteral("variant \"%1\"").arg(var.label.trimmed());
          r.plan.warnings << QStringLiteral(
                                 "Dropped %1 — %2; put it in the plan's top-level actions.")
                                 .arg(who, scopeDrop);
          continue;
        }
        r.plan.warnings += varWarnings;
        r.plan.variants.push_back(std::move(var));
      }
    }
    if (!parseAsk(obj.value("ask"), r.plan.ask, r.plan.warnings, &e)) {
      r.plan = {};
      r.error = e;
      return r;
    }
    // The substitute must not overstate what happened: "Done." only when the
    // plan actually carries work — an empty plan says so, since a bare "Done."
    // there reads as a success that never occurred.
    if (replyOmitted) {
      if (!r.plan.actions.isEmpty() || !r.plan.variants.isEmpty() ||
          !r.plan.ask.options.isEmpty()) {
        r.plan.reply = QStringLiteral("Done.");
        r.plan.warnings << QStringLiteral("The model omitted its reply — the plan still ran");
      } else {
        r.plan.reply =
            QStringLiteral("The model returned an empty plan — nothing was changed.");
      }
    }
    r.ok = true;
    return r;
  }

  QString askAnswerText(const QStringList& pickedLabels, const QString& custom) {
    const QString typed = custom.trimmed();
    QString answer;
    if (!typed.isEmpty()) {
      answer = typed;
    } else {
      QStringList kept;
      for (const QString& l : pickedLabels) {
        if (!l.trimmed().isEmpty()) kept << l.trimmed();
      }
      answer = kept.join(QStringLiteral(", "));
    }
    return answer.size() > kMaxAskAnswer ? answer.left(kMaxAskAnswer) : answer;
  }

  QString sanitizeLabel(const QString& label) {
    QString out;
    for (const QChar c : label) {
      if (c.isLetterOrNumber() || c.isSpace() || c == QLatin1Char('-') ||
          c == QLatin1Char('_'))
        out.append(c);
    }
    // Collapse the runs left behind by dropped symbols, then bound the length.
    return out.simplified().left(40);
  }

}  // namespace stencil::llm

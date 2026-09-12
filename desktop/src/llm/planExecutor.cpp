#include "planExecutor.hpp"

#include "canvasWidget.hpp"
#include "colorNames.hpp"
#include "cropSpec.hpp"
#include "formulaParser.hpp"
#include "opRegistry.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace stencil::llm {

  namespace {

    // Resolve a crop action's edge tokens to a pixel rect in rotated-original
    // space — the same core cropSpec path the CLI's --crop drives
    // (core/cliApi.cpp stencil_cli_resolveCrop), with the CLI's px-per-cm
    // derivation (image dims over the page) and clamping.
    bool resolveCrop(const Action& a, const QSize& imageSize, const core::PageSize& page,
                     core::CropRect& out, QString* err) {
      core::CropSpec spec;
      if (!a.x1.isEmpty()) spec.x1 = a.x1.toStdString();
      if (!a.x2.isEmpty()) spec.x2 = a.x2.toStdString();
      if (!a.y1.isEmpty()) spec.y1 = a.y1.toStdString();
      if (!a.y2.isEmpty()) spec.y2 = a.y2.toStdString();
      if (!a.aspect.isEmpty()) spec.aspect = a.aspect.toStdString();

      const double w = imageSize.width();
      const double h = imageSize.height();
      core::CropResolveParams p;
      p.imageW = w;
      p.imageH = h;
      p.pxPerCmX = page.width > 0 ? w / page.width : 0;
      p.pxPerCmY = page.height > 0 ? h / page.height : 0;
      p.pageWidth = page.width;
      p.pageHeight = page.height;
      const bool album = core::isAlbumOrientation(w, h);

      const auto rect = core::resolveCropRect(spec, p, album);
      if (!rect) {
        if (err) *err = QStringLiteral("crop: could not resolve the crop spec");
        return false;
      }
      const int iw = static_cast<int>(std::lround(w));
      const int ih = static_cast<int>(std::lround(h));
      const int x = std::clamp(static_cast<int>(std::lround(rect->x)), 0, std::max(0, iw));
      const int y = std::clamp(static_cast<int>(std::lround(rect->y)), 0, std::max(0, ih));
      const int cw = std::clamp(static_cast<int>(std::lround(rect->width)), 0, iw - x);
      const int ch = std::clamp(static_cast<int>(std::lround(rect->height)), 0, ih - y);
      if (cw <= 0 || ch <= 0) {
        if (err) *err = QStringLiteral("crop: the resolved region is empty");
        return false;
      }
      out = {static_cast<double>(x), static_cast<double>(y), static_cast<double>(cw),
             static_cast<double>(ch)};
      return true;
    }

    // Running model-frame → current-frame map (contract §1 coordinate
    // re-mapping): plan coordinates are in the frame of the image the model was
    // shown; earlier crop/rotate actions change that frame, so layout points go
    // through this affine before drawing. Crop composes a translation by minus
    // the resolved rect origin; rotate composes the app's own quarter-turn point
    // mapping (core::rotateLinePointsQuarter). Ops that REBUILD the image
    // (blank/frame/clear/openUrl) reset it — their result is a fresh frame.
    struct FrameMap {
      double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

      void reset() { *this = FrameMap{}; }
      void composeCrop(const core::CropRect& r) {
        tx -= r.x;
        ty -= r.y;
      }
      // `w`/`h` are the working-image dims BEFORE this quarter turn. Same
      // mapping as core::rotateLinePointsQuarter: cw (x,y)→(h-y,x); ccw (y,w-x).
      void composeRotate(bool clockwise, double w, double h) {
        const FrameMap m = *this;
        if (clockwise) {
          a = -m.c; b = -m.d; tx = h - m.ty;
          c = m.a;  d = m.b;  ty = m.tx;
        } else {
          a = m.c;  b = m.d;  tx = m.ty;
          c = -m.a; d = -m.b; ty = w - m.tx;
        }
      }
      core::Point map(const core::Point& p) const {
        return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty};
      }
    };

    bool applyAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                     QStringList* notes, QString* err) {
      // §13 forbidden-op tooth: no registered op uses a forbidden name (a test
      // pins that), but reject defensively even if one somehow appears.
      if (rejectForbiddenOp(opName(a.op), err)) return false;
      switch (a.op) {
        case OpKind::Crop: {
          if (!target.hasImage()) {
            *err = QStringLiteral("crop: no working image");
            return false;
          }
          core::CropRect rect;
          if (!resolveCrop(a, target.effectiveOriginalSize(), target.pageCm(), rect, err))
            return false;
          if (!target.applyCropRect(rect)) {
            *err = QStringLiteral("crop: could not apply the crop");
            return false;
          }
          frame.composeCrop(rect);
          return true;
        }
        case OpKind::Rotate: {
          if (!target.hasImage()) {
            *err = QStringLiteral("rotate: no working image");
            return false;
          }
          for (int i = 0; i < a.times; ++i) {
            const QSize s = target.workingSize();  // dims BEFORE this turn
            frame.composeRotate(!a.rotateLeft, s.width(), s.height());
            target.rotateQuarter(!a.rotateLeft);
          }
          return true;
        }
        case OpKind::Filter:
          target.setImageFilter(a.mode, a.tint);
          return true;
        case OpKind::Layout: {
          if (!target.hasImage()) {
            *err = QStringLiteral("layout: no working image");
            return false;
          }
          // Model-frame → current frame, then clamp into the working image's
          // bounds before drawing (contract §1).
          const QSize ws = target.workingSize();
          core::Lines lines = a.lines;
          for (core::Line& line : lines)
            for (core::Point& p : line.points) {
              p = frame.map(p);
              p.x = std::clamp(p.x, 0.0, static_cast<double>(ws.width()));
              p.y = std::clamp(p.y, 0.0, static_cast<double>(ws.height()));
            }
          target.setLayoutLines(lines);
          return true;
        }
        case OpKind::Formula: {
          // §2 `enabled` form: the allow-formulas toggle, nothing per-axis.
          if (a.formulaEnabled >= 0) {
            target.setFormulasEnabled(a.formulaEnabled == 1);
            return true;
          }
          // "" clears the axis (identity) — nothing to grammar-check. A
          // non-empty expr is validated again by the core formula engine
          // before use (contract §2).
          if (!a.expr.isEmpty() &&
              !core::FormulaParser::validate(a.expr.toStdString(),
                                             a.axis.toLatin1())) {
            *err = QStringLiteral("formula: the core parser rejected \"%1\"").arg(a.expr);
            return false;
          }
          target.setFormula(a.axis, a.expr);
          return true;
        }
        case OpKind::Page:
          // §2: format OR custom cm dims (exactly one — the parser enforced it).
          if (a.widthCm > 0)
            target.setPageCustom(a.widthCm, a.heightCm);
          else
            target.setPageFormat(a.format.toUpper());
          return true;
        case OpKind::Blank: {
          if (!target.newBlank(a.color, a.format.toUpper(), a.widthCm, a.heightCm, err))
            return false;
          frame.reset();  // a fresh image is a fresh frame
          return true;
        }
        case OpKind::Undo:
        case OpKind::Redo: {
          const bool redo = a.op == OpKind::Redo;
          const char* name = redo ? "redo" : "undo";
          if (inVariant) {  // parse-banned; defensive only
            *err = QStringLiteral("%1: not allowed inside a variant").arg(QLatin1String(name));
            return false;
          }
          const int done = target.stepHistory(redo, a.steps);
          if (done < 0) {
            *err = QStringLiteral("%1: edit history is not available here")
                       .arg(QLatin1String(name));
            return false;
          }
          // §2: one step is one HISTORY entry — say so when steps run out.
          if (done < a.steps && notes)
            *notes << (done == 0
                           ? QStringLiteral("%1: nothing to %1").arg(QLatin1String(name))
                           : QStringLiteral("%1: only %2 of %3 step(s) available")
                                 .arg(QLatin1String(name))
                                 .arg(done)
                                 .arg(a.steps));
          // History steps rebuild earlier image states — the model-frame map
          // no longer describes them.
          if (done > 0) frame.reset();
          return true;
        }
        case OpKind::Frame: {
          if (inVariant) {
            *err = QStringLiteral("frame: not valid inside a variant");
            return false;
          }
          if (!target.isVideoInput()) {
            *err = QStringLiteral("frame: the current input is not a video");
            return false;
          }
          if (!target.extractFrames(a.indices, err)) return false;
          frame.reset();
          return true;
        }
        case OpKind::OpenUrl: {
          if (inVariant) {
            *err = QStringLiteral("openUrl: not allowed inside a variant");
            return false;
          }
          // The model may only ECHO the user: the exact URL must appear in the
          // user's own messages this conversation (the `connect` stance — a
          // plan can never introduce a host).
          if (!target.userTypedText().contains(a.url)) {
            *err = QStringLiteral(
                       "openUrl blocked: \"%1\" is not a URL you gave in this conversation")
                       .arg(a.url);
            return false;
          }
          if (!target.openUrl(a.url, a.incognito, err)) return false;
          frame.reset();  // the loaded picture is a fresh frame
          return true;
        }
        case OpKind::OpenFile: {
          if (inVariant) {
            *err = QStringLiteral("openFile: not allowed inside a variant");
            return false;
          }
          // The same echo rule as openUrl, for the filesystem: the assistant reads
          // only where the user themselves pointed it.
          if (!pathEchoedIn(target.userTypedText(), a.path)) {
            *err = QStringLiteral(
                       "openFile blocked: \"%1\" is not a path you gave in this conversation")
                       .arg(a.path);
            return false;
          }
          if (!target.openFile(a.path, err)) return false;
          frame.reset();  // the loaded picture is a fresh frame
          return true;
        }
        // §2.1 multi-image ops (parse-banned in variants; the guards here
        // are defensive only)
        case OpKind::Image: {
          if (inVariant) {
            *err = QStringLiteral("image: not allowed inside a variant");
            return false;
          }
          // An index this turn cannot satisfy costs the ACTION, not the plan.
          QString why;
          if (!target.loadAttachment(a.index, &why) && notes)
            *notes << QStringLiteral("Skipped switching to attached image %1 — %2")
                          .arg(a.index)
                          .arg(why);
          frame.reset();  // the attachment is a fresh image, so a fresh frame
          return true;
        }
        case OpKind::Save: {
          if (inVariant) {
            *err = QStringLiteral("save: not allowed inside a variant");
            return false;
          }
          // Saving nothing is a skipped action, never a failed plan.
          if (!target.hasImage()) {
            if (notes) *notes << QStringLiteral("Skipped save — no working image to save");
            return true;
          }
          // A destination is honoured only when the user wrote it (§10's echo rule);
          // an unechoed one costs the destination, not the save.
          QString dest = a.path;
          if (!dest.isEmpty() && !pathEchoedIn(target.userTypedText(), dest)) {
            if (notes)
              *notes << QStringLiteral("Saved to the usual place — \"%1\" is not a path you "
                                       "gave in this conversation")
                            .arg(dest);
            dest.clear();
          }
          return target.saveProject(a.name, dest, err);
        }
        case OpKind::ClearChat:
        case OpKind::Dialog:
          // executePlan defers these past every other action; reaching here
          // means a variant/preview slipped through the parse ban.
          *err = QStringLiteral("editor-settings ops are not allowed inside a variant");
          return false;
        // §10 editor-settings ops (parse-banned in variants; the inVariant
        // guard below is defensive only)
        case OpKind::Theme:
        case OpKind::Accent:
        case OpKind::LineStyle:
        case OpKind::Units:
        case OpKind::View:
        case OpKind::Clear:
        case OpKind::Connect:
        case OpKind::Disconnect:
        case OpKind::Copy:
        case OpKind::RemoveProject:
        case OpKind::ClearProjects:
        case OpKind::Compare:
        case OpKind::Zoom:
        case OpKind::RenameProject:
        case OpKind::ProjectColor:
        case OpKind::BlankColor:
        case OpKind::OpenProject:
        case OpKind::ChatPanel:
        case OpKind::Incognito: {
          if (inVariant) {
            *err = QStringLiteral("editor-settings ops are not allowed inside a variant");
            return false;
          }
          // §10: the true+note contract — a non-empty note (no active project,
          // non-blank editor, unknown/ambiguous name, declined confirm) is
          // surfaced and the plan continues; false fails the plan.
          const auto noted = [&](const char* opName, bool ok, const QString& note) {
            if (!ok) { *err = note; return false; }
            if (!note.isEmpty() && notes)
              *notes << QStringLiteral("%1: %2").arg(QLatin1String(opName), note);
            return true;
          };
          switch (a.op) {
            case OpKind::Theme: target.setTheme(a.mode); return true;
            case OpKind::Accent: {
              // §10: a PRESET persists and syncs; unknown preset names are the
              // target's note+skip. A raw hex keeps the current behaviour.
              if (!a.preset.isEmpty()) {
                QString note;
                target.setAccentPreset(a.preset, &note);
                if (!note.isEmpty() && notes)
                  *notes << QStringLiteral("accent: %1").arg(note);
                return true;
              }
              target.setAccent(a.color);
              return true;
            }
            case OpKind::LineStyle:
              // §10: fillColor is a BROWSER-only control — this editor notes
              // and skips that field, applying the rest.
              if (!a.fillColor.isEmpty() && notes)
                *notes << QStringLiteral(
                    "lineStyle: fillColor is a browser-editor control — skipped here");
              target.setDefaultLineStyle(a);
              return true;
            case OpKind::Units: target.setUnits(a.value); return true;
            case OpKind::View: target.setViewVisibility(a.viewPoints, a.viewLines); return true;
            case OpKind::Clear: target.clearImage(); frame.reset(); return true;
            case OpKind::Connect: return target.connectServer(a.server, err);
            case OpKind::Copy:
              // §10: copy what:"layout" needs drawn lines; the default image
              // form needs a working image — both are a note+skip, never a
              // failed plan.
              if (a.what == QLatin1String("layout")) {
                if (!target.hasDrawnLines()) {
                  if (notes)
                    *notes << QStringLiteral("Skipped copy — no drawn lines to copy");
                  return true;
                }
                return target.copyLayout(err);
              }
              if (!target.hasImage()) {
                if (notes) *notes << QStringLiteral("Skipped copy — no working image to copy");
                return true;
              }
              return target.copyImage(err);
            case OpKind::RemoveProject: {
              QString note;
              return noted("removeProject",
                           target.removeProjectNamed(a.name, a.current, &note), note);
            }
            case OpKind::ClearProjects: {
              QString note;
              return noted("clearProjects", target.clearProjects(a.current, &note), note);
            }
            case OpKind::Compare:
              return target.setCompare(a.mode, a.split, err);
            case OpKind::Zoom:
              return target.setZoom(a.percent, a.fit, err);
            case OpKind::RenameProject: {
              QString note;
              return noted("renameProject", target.renameActiveProject(a.name, &note),
                           note);
            }
            case OpKind::ProjectColor: {
              QString note;
              return noted("projectColor", target.setProjectColor(a.color, &note), note);
            }
            case OpKind::BlankColor: {
              QString note;
              return noted("blankColor", target.setBlankColor(a.color, &note), note);
            }
            case OpKind::OpenProject: {
              QString note;
              if (!noted("openProject", target.openProjectNamed(a.name, a.current, &note), note))
                return false;
              // An actually opened project is a fresh working image, so a
              // fresh frame (a noted skip left the canvas alone, where the
              // reset is harmless anyway).
              if (note.isEmpty()) frame.reset();
              return true;
            }
            case OpKind::Incognito: {
              QString note;
              return noted("incognito", target.setIncognito(a.incognito, &note), note);
            }
            case OpKind::ChatPanel: {
              QString note;
              return noted("chatPanel", target.setChatPlacement(a.chatOpen, a.dock, &note), note);
            }
            default: return target.disconnectServer(a.server, err);
          }
        }
      }
      *err = QStringLiteral("unhandled op");
      return false;
    }

  }  // namespace

  bool PlanTarget::setBlankColor(const QString&, QString* note) {
    if (note) *note = QStringLiteral("blankColor: not available here");
    return false;
  }

  namespace {
    // `needle` appears in `text` as a complete path token: at the end, or followed by
    // whitespace or sentence punctuation — never by another path segment.
    bool namedAsPathIn(const QString& text, const QString& needle) {
      for (int at = text.indexOf(needle); at >= 0; at = text.indexOf(needle, at + 1)) {
        const int after = at + needle.size();
        if (after >= text.size()) return true;
        const QChar c = text.at(after);
        if (c.isSpace()) return true;
        if (QStringLiteral(",;:\"')]}!?").contains(c)) return true;
        // A dot ends the token only when it ends the sentence, so "~/Downloads.png" (a file
        // the user named) never grants the "~/Downloads" folder.
        if (c == QLatin1Char('.') &&
            (after + 1 >= text.size() || text.at(after + 1).isSpace()))
          return true;
      }
      return false;
    }
  }  // namespace

  bool pathEchoedIn(const QString& typed, const QString& path) {
    if (typed.contains(path)) return true;
    if (path.contains(QLatin1String(".."))) return false;
    int end = path.size();
    for (int slash = path.lastIndexOf(QLatin1Char('/'), end - 1); slash > 0;
         slash = path.lastIndexOf(QLatin1Char('/'), end - 1)) {
      if (namedAsPathIn(typed, path.left(slash))) return true;
      end = slash;
    }
    return false;  // a bare "/" grants nothing
  }

  bool CanvasPlanTarget::newBlank(const QString& color, const QString& isoName,
                                  double widthCm, double heightCm, QString* err) {
    core::PageSize page = page_;
    if (!isoName.isEmpty()) {
      const core::PageSize named = core::namedPageSize(isoName.toStdString());
      if (named.width > 0) page = named;
    }
    // §2: explicit cm dims override the format.
    if (widthCm > 0 && heightCm > 0) page = {widthCm, heightCm};
    const auto rgba = core::parseColor(color.toStdString());
    if (!rgba) {
      if (err) *err = QStringLiteral("blank: unknown colour \"%1\"").arg(color);
      return false;
    }
    const core::SizePx px = core::defaultBlankSizePx(page, 96.0);
    QImage img(px.width, px.height, QImage::Format_RGB32);
    img.fill(QColor(rgba->r, rgba->g, rgba->b));
    page_ = page;
    canvas_->setPageCm(page_.width, page_.height);
    canvas_->loadFromImage(
        img,
        core::CropRect{0, 0, static_cast<double>(px.width), static_cast<double>(px.height)},
        0);
    blank_ = true;
    return true;
  }

  bool CanvasPlanTarget::setBlankColor(const QString& color, QString* note) {
    if (!blank_ || !canvas_->hasImage()) {
      // §10: valid only on a BLANK project — a note+skip, never a plan error.
      if (note) *note = QStringLiteral("only a blank page's background can be recoloured");
      return true;
    }
    const auto rgba = core::parseColor(color.toStdString());
    if (!rgba) {
      if (note) *note = QStringLiteral("unknown colour \"%1\"").arg(color);
      return true;
    }
    // Recolour in place, KEEPING the drawn lines (the point of the op — a
    // fresh `blank` would destroy them).
    const core::Lines keep = canvas_->lines();
    const QSize sz = canvas_->image().size();
    QImage img(sz, QImage::Format_RGB32);
    img.fill(QColor(rgba->r, rgba->g, rgba->b));
    canvas_->loadFromImage(
        img,
        core::CropRect{0, 0, static_cast<double>(sz.width()),
                       static_cast<double>(sz.height())},
        0);
    if (!keep.empty()) canvas_->setLines(keep);
    blank_ = true;  // loadFromImage doesn't change what this canvas holds
    return true;
  }


  ExecResult executePlan(const OpPlan& plan, PlanTarget& target) {
    ExecResult res;
    QString err;
    FrameMap frame;  // model frame → working frame (contract §1)
    bool clearChatLast = false;
    // §10 dialog: modal, so it opens once the plan is done — and only the LAST one asked
    // for ("close this and open that" ends with that one open; browser flushDeferred).
    Action dialogLast;
    bool hasDialog = false;
    for (const Action& a : plan.actions) {
      // §10 clearChat is DEFERRED to the end of the plan — wherever the model
      // put it, every other action (and the variants) runs first.
      if (a.op == OpKind::ClearChat) { clearChatLast = true; continue; }
      if (a.op == OpKind::Dialog) { dialogLast = a; hasDialog = true; continue; }
      if (!applyAction(a, target, frame, /*inVariant=*/false, &res.notes, &err)) {
        res.error = err;
        return res;
      }
      res.changed = true;
    }
    if (!plan.variants.isEmpty()) {
      // Each variant branches from the image AFTER the top-level actions.
      const QImage snapshot = target.renderResult();
      if (snapshot.isNull()) {
        res.error = QStringLiteral("variants need a working image");
        return res;
      }
      for (int i = 0; i < plan.variants.size(); ++i) {
        const Variant& v = plan.variants.at(i);
        CanvasPlanTarget sandbox(snapshot, target.pageCm());
        // Variant coordinates are model-frame too — start from the mapping the
        // top-level actions accumulated (the state the variant branches from).
        FrameMap vframe = frame;
        for (const Action& a : v.actions) {
          if (!applyAction(a, sandbox, vframe, /*inVariant=*/true, &res.notes, &err)) {
            res.error = QStringLiteral("variant %1: %2").arg(i + 1).arg(err);
            return res;
          }
        }
        QString label = sanitizeLabel(v.label);
        if (label.isEmpty()) label = QStringLiteral("variant %1").arg(i + 1);
        res.variants.append({label, sandbox.renderResult()});
      }
    }
    if (hasDialog) {
      // §10: a window in front of the user is the LAST thing a turn does — the edits
      // and the reply land first, then the dialog (browser opPlan.js `deferred`).
      QString note;
      if (!target.openDialog(dialogLast.current ? QString() : dialogLast.dialog, &note)) {
        res.error = note;
        return res;
      }
      if (!note.isEmpty()) res.notes << QStringLiteral("dialog: %1").arg(note);
      res.changed = true;
    }
    if (clearChatLast) {
      // §10: the surface's clear flow (confirm included) runs last; a note
      // (declined confirm) surfaces without failing the plan.
      QString note;
      if (!target.clearChat(&note)) {
        res.error = note;
        return res;
      }
      if (!note.isEmpty()) res.notes << QStringLiteral("clearChat: %1").arg(note);
      res.changed = true;
    }
    res.ok = true;
    return res;
  }
}  // namespace stencil::llm


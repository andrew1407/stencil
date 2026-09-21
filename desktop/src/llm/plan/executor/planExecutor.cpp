// A validated op plan against a PlanTarget (llm-contract §1): every action in order, stopping at the
// first failure. The three op groups live in planExecutor{Image,Edit,State}.cpp.
#include "planExecutorParts.hpp"

#include "CanvasWidget.hpp"
#include "opRegistry.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace stencil::llm {

  using namespace exec;

  namespace {

    bool applyAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                     QStringList* notes, QString* err) {
      // §13 forbidden-op tooth: no registered op uses a forbidden name (a test
      // pins that), but reject defensively even if one somehow appears.
      if (rejectForbiddenOp(opName(a.op), err)) return false;
      bool handled = false;
      bool ok = applyImageAction(a, target, frame, inVariant, notes, &handled, err);
      if (handled) return ok;
      ok = applyEditAction(a, target, frame, inVariant, notes, &handled, err);
      if (handled) return ok;
      ok = applyStateAction(a, target, frame, inVariant, notes, &handled, err);
      if (handled) return ok;
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
    core::PageSize page = this->page;
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
    this->page = page;
    canvas->setPageCm(this->page.width, this->page.height);
    canvas->loadFromImage(
        img,
        core::CropRect{0, 0, static_cast<double>(px.width), static_cast<double>(px.height)},
        0);
    blank = true;
    return true;
  }

  bool CanvasPlanTarget::setBlankColor(const QString& color, QString* note) {
    if (!blank || !canvas->hasImage()) {
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
    const core::Lines keep = canvas->getLines();
    const QSize sz = canvas->getImage().size();
    QImage img(sz, QImage::Format_RGB32);
    img.fill(QColor(rgba->r, rgba->g, rgba->b));
    canvas->loadFromImage(
        img,
        core::CropRect{0, 0, static_cast<double>(sz.width()),
                       static_cast<double>(sz.height())},
        0);
    if (!keep.empty()) canvas->setLines(keep);
    blank = true;  // loadFromImage doesn't change what this canvas holds
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
      if (a.op == OpKind::CLEAR_CHAT) { clearChatLast = true; continue; }
      if (a.op == OpKind::DIALOG) { dialogLast = a; hasDialog = true; continue; }
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


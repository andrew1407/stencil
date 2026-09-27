// What every PlanTarget shares (llm-contract §1–2, §10): the formula names, the path echo rule and
// the sandbox's blank page. The run is planExecutorRun.cpp; the op groups planExecutor{Image,Edit,State}.cpp.
#include "planExecutorParts.hpp"
#include "CanvasScene.hpp"
#include "planExecutor.hpp"

#include <QColor>

namespace stencil::llm {

  core::FormulaContext PlanTarget::formulaContext() const {
    core::FormulaContext ctx;
    const core::PageSize page = pageCm();
    ctx.pageWidthCm = page.width;
    ctx.pageHeightCm = page.height;
    if (hasImage()) {
      const QSize px = workingSize();
      ctx.imageWidth = px.width();
      ctx.imageHeight = px.height();
    }
    return ctx;
  }

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
}  // namespace stencil::llm


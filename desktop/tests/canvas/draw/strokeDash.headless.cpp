// A dashed or dotted stroke measures its dashes in px, as the browser's setLineDash does
// (constants.json STROKE_DASH), whatever the line's width: a QPen counts in pen widths.
#include "CanvasWidget.hpp"
#include "strokeDash.hpp"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;
namespace dash = stencil::gui::strokeDash;

namespace {
  QList<qreal> tableRow(const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QList<qreal> out;
    const QJsonObject t = QJsonDocument::fromJson(f.readAll()).object().value("STROKE_DASH").toObject();
    for (const QJsonValue& v : t.value(QLatin1String(key)).toArray()) out << v.toDouble();
    return out;
  }

  // The x of each dash's start along row y: where ink begins after a gap.
  QList<int> dashStarts(const QImage& img, int y) {
    QList<int> out;
    bool inked = false;
    for (int x = 0; x < img.width(); ++x) {
      const bool ink = qGray(img.pixel(x, y)) < 128;
      if (ink && !inked) out << x;
      inked = ink;
    }
    return out;
  }

  // One horizontal line of `style` and `width` across a white page, points hidden.
  QImage render(const char* style, double width) {
    CanvasWidget canvas;
    canvas.setShowPoints(false);
    QImage page(240, 40, QImage::Format_RGB32);
    page.fill(Qt::white);
    canvas.loadFromImage(page, stencil::core::CropRect{0, 0, 240, 40}, 0);
    stencil::core::Line line;
    line.points = {{20, 20.5}, {220, 20.5}};
    line.color = "#000000";
    line.thickness = width;
    line.style = style;
    canvas.setLines({line});
    return canvas.renderToImage(/*withOverlay=*/true);
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  check(tableRow("dashed") == dash::FALLBACK_DASHED, "the dashed fallback is the table's row");
  check(tableRow("dotted") == dash::FALLBACK_DOTTED, "the dotted fallback is the table's row");
  check(dash::patternPx("dashed") == tableRow("dashed"), "the stroke reads the table");
  check(dash::patternPx("solid").isEmpty(), "a solid line has no pattern");

  const qreal dashedPeriod = dash::FALLBACK_DASHED[0] + dash::FALLBACK_DASHED[1];
  const qreal dottedPeriod = dash::FALLBACK_DOTTED[0] + dash::FALLBACK_DOTTED[1];
  // Round caps eat width/2 of each gap on both sides, so the widths stay under the 5px gap.
  for (const double width : {1.0, 2.0, 3.0}) {
    for (const auto& [style, period] : {std::pair{"dashed", dashedPeriod}, std::pair{"dotted", dottedPeriod}}) {
      const QList<int> starts = dashStarts(render(style, width), 20);
      bool even = starts.size() >= 3;
      for (int i = 1; even && i < starts.size() - 1; ++i)
        even = qAbs(starts[i + 1] - starts[i] - period) <= 1.0;
      std::printf("  %s @ %.0fpx: %lld dashes\n", style, width, static_cast<long long>(starts.size()));
      check(even, "the dash period is the table's px at every line width");
    }
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

// The theme lens overlay (app/theme/ThemeLens, browser twin ui/drag/themeLens.js) on a bare host: a
// disc at the pointer shows the photograph it was handed with its picture region inverted, a rim
// marks its edge, the host shows around it, and the pointer never lands on it. Pixel reads follow
// the device pixel ratio, so QT_SCALE_FACTOR=2 probes it on a Retina-sized photograph.
#include "ThemeLens.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QWidget>

#include "../../support/check.hpp"

using stencil::gui::ThemeLens;

namespace {
  const QColor GROUND(20, 20, 20);     // the live window
  const QColor OTHER(240, 240, 240);   // the window as the other theme paints it
  const QColor PICTURE(10, 200, 100);

  class Host : public QWidget {
   public:
    using QWidget::QWidget;

   protected:
    void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(rect(), GROUND); }
  };

  QColor at(const QImage& shot, int x, int y) {
    const qreal dpr = shot.devicePixelRatio();
    return shot.pixelColor(qRound((x + 0.5) * dpr - 0.5), qRound((y + 0.5) * dpr - 0.5));
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  Host host;
  host.resize(600, 400);
  host.show();
  const qreal dpr = host.devicePixelRatioF();
  QPixmap other(host.size() * dpr);
  other.setDevicePixelRatio(dpr);
  other.fill(OTHER);
  const QRect picture(300, 0, 300, 400);
  QPainter(&other).fillRect(picture, PICTURE);

  auto* lens = new ThemeLens(&host, other, picture);
  check(!lens->isVisible() && lens->testAttribute(Qt::WA_TransparentForMouseEvents),
        "the lens waits for the pointer and never takes it");
  lens->follow(host.mapToGlobal(QPoint(150, 200)));
  QImage shot = host.grab().toImage();
  check(lens->isVisible() && at(shot, 150, 200) == OTHER, "its disc shows the other theme at the pointer");
  check(at(shot, 150, 200 - ThemeLens::RADIUS + 10) == OTHER, "…out to its radius");
  check(qGray(at(shot, 150, 200 - ThemeLens::RADIUS - 1).rgb()) > qGray(GROUND.rgb()) + 40, "a light rim marks its edge");
  check(at(shot, 150, 200 - ThemeLens::REACH - 4) == GROUND, "past it the window shows");

  lens->follow(host.mapToGlobal(QPoint(320, 200)));
  shot = host.grab().toImage();
  check(at(shot, 350, 200) == QColor(255 - PICTURE.red(), 255 - PICTURE.green(), 255 - PICTURE.blue()),
        "over the picture, which has no theme, the disc inverts it");
  check(at(shot, 290, 200) == OTHER, "…and nowhere else");
  check(at(shot, 150, 200) == GROUND, "the disc it left shows the window again");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

// CropDialog (dialogs/crop/CropDialogDrag.cpp) on a small screen: the whole window fits it, footer
// included, and every resize re-fits the picture (CropPreview, CropDialog.cpp) while the crop rect
// stays in image pixels. Browser twin: tests/ui/modal/cropFit.test.js.
#include "CropDialog.hpp"
#include "cropDialogParts.hpp"
#include "cropGeometry.hpp"

#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::CropDialog;
using stencil::gui::CropPreview;
using stencil::gui::INSET;
using stencil::gui::SCREEN_MARGIN;

namespace {
  // Two offscreen screens: a 1366x768 laptop, and one shorter than the window's own wish.
  constexpr const char* SCREENS = R"({"screens":[
    {"name":"laptop","x":0,"y":0,"width":1366,"height":768,"logicalDpi":96,"logicalBaseDpi":96,"dpr":1},
    {"name":"short","x":1366,"y":0,"width":1024,"height":560,"logicalDpi":96,"logicalBaseDpi":96,"dpr":1}]})";

  void settle() {
    for (int i = 0; i < 4; ++i) QApplication::processEvents();
  }

  bool inside(const QWidget* w, const QWidget* dlg) {
    return dlg->rect().contains(QRect(w->mapTo(dlg, QPoint(0, 0)), w->size()));
  }

  bool buttonsInside(const QDialog& dlg) {
    int seen = 0;
    for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
      if (!b->isVisible()) continue;
      ++seen;
      if (!inside(b, &dlg)) return false;
    }
    return seen >= 4;   // Close, Album/Portrait, Cancel, Apply Crop
  }

  // The picture keeps its aspect and touches the stage on one axis, inside the handle inset.
  bool fills(const CropPreview* p, int iw, int ih) {
    const QRect r = p->paintedRect();
    const bool aspect = std::abs(r.width() * double(ih) - r.height() * double(iw)) <= std::max(iw, ih);
    const bool within = p->rect().contains(r);
    const int slackW = p->width() - r.width(), slackH = p->height() - r.height();
    return aspect && within && std::min(slackW, slackH) <= 2 * INSET + 2;   // plus rounding
  }

  void press(QWidget* w, QEvent::Type type, const QPointF& at) {
    QMouseEvent ev(type, at, w->mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                   type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
  }

  struct Opened {
    QWidget host;
    CropDialog* dlg = nullptr;
    CropPreview* preview = nullptr;
    QRect avail;
  };

  void open(Opened& o, const QImage& img, const char* screenName,
            const stencil::core::CropRect& initial = {}) {
    for (QScreen* s : QGuiApplication::screens())
      if (s->name() == QLatin1String(screenName)) { o.host.setScreen(s); o.avail = s->availableGeometry(); }
    o.dlg = new CropDialog(img, 21.0, 29.7, img.width() >= img.height(), initial, &o.host);
    o.preview = o.dlg->findChild<CropPreview*>();
    o.dlg->show();
    settle();
  }

  QImage picture(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    return img;
  }

  void opensInside(const char* screenName, int w, int h) {
    Opened o;
    open(o, picture(w, h), screenName);
    const QByteArray what = QByteArray(screenName) + " " + QByteArray::number(w) + "x" + QByteArray::number(h);
    check(o.preview != nullptr, ("a preview: " + what).constData());
    if (!o.preview) return;
    const QRect room = o.avail.adjusted(SCREEN_MARGIN, SCREEN_MARGIN, -SCREEN_MARGIN, -SCREEN_MARGIN);
    check(o.dlg->height() <= room.height() && o.dlg->width() <= room.width(),
          ("the window opens inside the screen less its margin: " + what).constData());
    check(buttonsInside(*o.dlg), ("every footer button lies inside the window: " + what).constData());
    check(fills(o.preview, w, h), ("the picture fills its stage, aspect kept: " + what).constData());
    o.dlg->reject();
  }
}

int main(int argc, char** argv) {
  QTemporaryDir dir;   // the platform reads its screens when the application is built
  const QString cfg = dir.filePath(QStringLiteral("screens.json"));
  {
    QFile f(cfg);
    if (f.open(QIODevice::WriteOnly)) f.write(SCREENS);
  }
  qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + cfg).toUtf8());
  QApplication app(argc, argv);
  check(QGuiApplication::screens().size() == 2, "the two configured screens are up");

  // Any aspect, on a laptop and on a screen shorter than the window would like.
  for (const char* screen : {"laptop", "short"}) {
    opensInside(screen, 4000, 3000);   // a photo
    opensInside(screen, 600, 4000);    // a very tall portrait
    opensInside(screen, 6000, 800);    // a panorama
    opensInside(screen, 64, 48);       // a tiny picture, upscaled
  }

  {
    // A resize re-fits the picture: larger grows it, smaller shrinks it, the crop rect never moves.
    Opened o;
    open(o, picture(3000, 2000), "laptop");
    if (o.preview) {
      const stencil::core::CropRect before = o.preview->cropRect();
      const QRect opened = o.preview->paintedRect();
      o.dlg->resize(o.dlg->width() + 200, o.dlg->height() + 160);
      settle();
      const QRect grown = o.preview->paintedRect();
      check(grown.width() > opened.width() && grown.height() > opened.height(),
            "a larger window paints a larger picture");
      check(fills(o.preview, 3000, 2000), "…still filling its stage");
      o.dlg->resize(o.dlg->width() - 500, o.dlg->height() - 300);
      settle();
      const QRect shrunk = o.preview->paintedRect();
      check(shrunk.width() < opened.width() && shrunk.height() < opened.height(),
            "a smaller window paints a smaller picture");
      check(buttonsInside(*o.dlg), "…and keeps its footer whole");
      const stencil::core::CropRect after = o.preview->cropRect();
      check(after.x == before.x && after.y == before.y && after.width == before.width &&
                after.height == before.height,
            "the crop rect stays in image pixels through every resize");
      o.dlg->resize(60, 60);
      settle();
      check(o.dlg->width() > 60 && o.dlg->height() > 60, "shrinking stops at a minimum");
      check(buttonsInside(*o.dlg), "…where the footer is still whole");
      check(o.preview->paintedRect().width() >= 100 && o.preview->paintedRect().height() >= 60,
            "…and the picture still has room");
      o.dlg->reject();
    }
  }

  {
    // A tall picture opens at the footer's one line; a flip to the longer face must not re-wrap it.
    Opened o;
    open(o, picture(1000, 1500), "laptop", {0, 0, 1000, 707});   // an album crop: the shorter face
    QPushButton* toggle = nullptr;
    for (QPushButton* b : o.dlg->findChildren<QPushButton*>())
      if (b->text() == QLatin1String("Album")) toggle = b;
    check(toggle != nullptr, "the Album/Portrait toggle shows its Album face");
    if (toggle && o.preview) {
      const int width = toggle->width();
      const QRect before = o.preview->paintedRect();
      toggle->click();
      settle();
      check(toggle->text() == QLatin1String("Portrait") && toggle->width() == width,
            "the toggle keeps its width through a flip");
      check(o.preview->paintedRect() == before, "…so the picture keeps its place");
    }
    o.dlg->reject();
  }

  {
    // The drawn box and the pointer agree at every scale: a drag of d display px moves the rect
    // by d / scale image px, and a press on a drawn corner takes that corner.
    Opened o;
    open(o, picture(3000, 2000), "laptop", {600, 400, 1414, 1000});   // clear of every edge
    for (int pass = 0; o.preview && pass < 2; ++pass) {
      if (pass == 1) { o.dlg->resize(o.dlg->width() - 260, o.dlg->height() - 180); settle(); }
      CropPreview* p = o.preview;
      const QRect img = p->paintedRect();
      const double scale = img.width() / 3000.0;
      const stencil::core::CropRect r0 = p->cropRect();
      const QPointF centre(img.x() + (r0.x + r0.width / 2) * scale, img.y() + (r0.y + r0.height / 2) * scale);
      press(p, QEvent::MouseButtonPress, centre);
      press(p, QEvent::MouseMove, centre + QPointF(-30, -20));
      press(p, QEvent::MouseButtonRelease, centre + QPointF(-30, -20));
      const stencil::core::CropRect r1 = p->cropRect();
      const bool moved = std::abs((r1.x - r0.x) * scale + 30) <= 1.5 && std::abs((r1.y - r0.y) * scale + 20) <= 1.5;
      check(moved, pass ? "a drag maps to image px after a resize" : "a drag maps to image px as opened");
      const QPointF corner(img.x() + (r1.x + r1.width) * scale, img.y() + (r1.y + r1.height) * scale);
      press(p, QEvent::MouseButtonPress, corner);
      press(p, QEvent::MouseMove, corner + QPointF(-40, -40));
      press(p, QEvent::MouseButtonRelease, corner + QPointF(-40, -40));
      const stencil::core::CropRect r2 = p->cropRect();
      check(r2.width < r1.width && r2.x == r1.x && r2.y == r1.y,
            pass ? "the drawn corner is the grabbed corner after a resize"
                 : "the drawn corner is the grabbed corner");
    }
    if (o.dlg) o.dlg->reject();
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures ? 1 : 0;
}

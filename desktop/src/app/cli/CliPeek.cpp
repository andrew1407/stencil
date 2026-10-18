#include "CliPeek.hpp"

#include <QPixmap>

namespace stencil::gui {

  namespace {
    constexpr double PEEK_SHARE = 0.4;   // the cap, of the stage's width and height; magnified is twice that
    constexpr double PEEK_MIN = 0.2;     // the floor on its height, of the stage's; magnified is twice that
    constexpr int FRAME = 7;             // the label's QSS padding and border, each side
  }

  CliPeek::CliPeek(QWidget* parent, std::function<QImage()> editing, std::function<QImage()> original)
      : QLabel(parent), editing(std::move(editing)), original(std::move(original)) {
    setObjectName(QStringLiteral("cliPeek"));
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    hide();
  }

  void CliPeek::draw(Kind which, bool big) {
    picture = which == Kind::ORIGINAL ? original() : editing();
    if (picture.isNull()) {
      hideNow();
      return;
    }
    kind = which;
    magnified = big;
    // Natural size between a floor on its height and a cap on both: cli.css's min-height/max-*.
    const double share = big ? 2 : 1;
    const int w = qMax(1, int(boxW * share * PEEK_SHARE)), h = qMax(1, int(boxH * share * PEEK_SHARE));
    const int minH = qMax(1, int(boxH * share * PEEK_MIN));
    QImage fitted = picture;
    if (picture.height() < minH) fitted = picture.scaledToHeight(minH, Qt::SmoothTransformation);
    if (fitted.width() > w || fitted.height() > h) fitted = fitted.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setPixmap(QPixmap::fromImage(fitted));
    resize(fitted.size() + QSize(2 * FRAME, 2 * FRAME));
    show();
    raise();
    if (parentWidget()) parentWidget()->update();   // the stage lays its text out under the picture
  }

  void CliPeek::toggle(Kind which) {
    held = false;
    if (which == Kind::ORIGINAL) {
      draw(kind == Kind::ORIGINAL ? Kind::EDITING : Kind::ORIGINAL, false);
      return;
    }
    if (kind) hideNow();
    else draw(Kind::EDITING, false);
  }

  void CliPeek::hold(Kind which, bool big) {
    if (kind && !held) return;
    held = true;
    draw(which, big);
  }

  void CliPeek::release() {
    if (!held) return;
    held = false;
    hideNow();
  }

  void CliPeek::hideNow() {
    kind.reset();
    magnified = false;
    hide();
    if (parentWidget()) parentWidget()->update();
  }

  void CliPeek::refresh() {
    if (kind) draw(*kind, magnified);
  }

  void CliPeek::fitTo(int stageWidth, int stageHeight) {
    boxW = stageWidth;
    boxH = stageHeight;
    if (kind) draw(*kind, magnified);
  }

}  // namespace stencil::gui

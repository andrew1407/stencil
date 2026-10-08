// The theme switch dragged out as a lens (browser twin js/ui/drag/themeLens.js): the other theme is
// photographed at idle once the pointer reaches the switch (or as a drag starts, if it is not) and
// only previewed; a release anywhere, or Escape, takes the lens down and switches nothing.
#include "MainWindow.hpp"
#include "ThemePainter.hpp"
#include "ThemeLens.hpp"
#include "CanvasWidget.hpp"
#include "iconDrag.hpp"
#include "../../support/skinPrefs.hpp"

#include "AppTooltip.hpp"

#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <utility>

namespace stencil::gui {

  namespace {
    // The pointer reaching or pressing the switch photographs the other theme once the loop is
    // idle, so the drag that may follow opens its lens at once; leaving drops the photograph.
    class LensPrimer : public QObject {
     public:
      LensPrimer(QObject* button, std::function<void()> prime, std::function<void()> forget)
          : QObject(button), prime(std::move(prime)), forget(std::move(forget)) {
        idle.setSingleShot(true);
        QObject::connect(&idle, &QTimer::timeout, this, [this] { this->prime(); });
      }

     protected:
      bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::Enter || e->type() == QEvent::MouseButtonPress) idle.start(0);
        if (e->type() == QEvent::Leave) { idle.stop(); forget(); }
        return false;
      }

     private:
      std::function<void()> prime, forget;
      QTimer idle;
    };
  }  // namespace

  void ThemePainter::installThemeLens() {
    for (QToolButton* b : w.findChildren<QToolButton*>()) {
      if (b->defaultAction() != w.acts.theme) continue;
      support::IconDragHooks drag;
      drag.ghostCentred = true;   // the switch sits in the lens's middle
      drag.start = [this](const QPoint&, const QPoint& global) {
        openLens(global);
        return true;
      };
      drag.move = [this](const support::IconDragPoint& p) {
        if (lens) static_cast<ThemeLens*>(lens.data())->follow(p.global);
      };
      drag.drop = [this](const support::IconDragPoint&) { closeLens(); };
      drag.cancel = [this] { closeLens(); };
      support::installIconDrag(b, std::move(drag));
      const auto prime = [this, b] { if (!support::iconDragActive(b)) primeLens(); };
      b->installEventFilter(new LensPrimer(b, prime, [this] { primed = QPixmap(); }));
    }
  }

  // As in the browser, the webcore skin opens no lens; nor does a wipe in flight or an open popover,
  // which the photograph would carry. The drag goes on without one.
  bool ThemePainter::lensAllowed() const {
    return !support::isWebcore() && !w.painted.swapping() && !w.pop.active && !w.tearingDown && w.isVisible();
  }

  QString ThemePainter::lensKey() const {
    return QStringLiteral("%1|%2|%3x%4").arg(w.painted.dark).arg(w.painted.accent).arg(w.width()).arg(w.height());
  }

  void ThemePainter::primeLens() {
    if (lens || !lensAllowed() || (!primed.isNull() && primedKey == lensKey())) return;
    primed = otherThemeShot();
    primedKey = lensKey();
  }

  void ThemePainter::openLens(const QPoint& global) {
    closeLens();
    if (!lensAllowed()) return;
    QPixmap shot = primedKey == lensKey() ? std::exchange(primed, QPixmap()) : QPixmap();
    if (shot.isNull()) shot = otherThemeShot();
    QRect picture;
    if (w.canvas->hasImage()) {
      const QWidget* port = w.scroll->viewport();
      picture = QRect(w.canvas->mapTo(&w, QPoint(0, 0)), w.canvas->size()) &
                QRect(port->mapTo(&w, QPoint(0, 0)), port->size());
    }
    auto* view = new ThemeLens(&w, std::move(shot), picture);
    view->follow(global);
    lens = view;
  }

  void ThemePainter::closeLens() { delete lens.data(); }

  // The window as the other theme paints it: restyled, photographed and put back with no event-loop
  // turn between, so neither restyle reaches the screen; the override is the session's alone.
  QPixmap ThemePainter::otherThemeShot() {
    const std::optional<bool> forced = support::forcedDark();
    w.painted.silent = true;
    support::setForcedDark(!w.painted.dark);
    w.applyTheme();
    QWidget* dust = appTooltip() ? appTooltip()->liveDust() : nullptr;   // a tip's, never the window's
    const bool dusty = dust && dust->isVisible();
    if (dusty) dust->hide();
    const QPixmap shot = w.grab();
    if (dusty) dust->show();
    if (forced) support::setForcedDark(*forced);
    else support::clearForcedDark();
    w.applyTheme();
    w.painted.silent = false;
    return shot;
  }

}  // namespace stencil::gui

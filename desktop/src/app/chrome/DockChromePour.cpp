// Sliding's picture for the side panel, the toolbar rows and the docked chat: it pours out of the toggle that
// opened them and back into it (support/genie) while their place slides open or shut on the same clock.
// Browser twin: ui/motion/control/tips.js pourFold and ui/chat/panel/openState.js.
#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "ChatDock.hpp"
#include "SelectionPanel.hpp"
#include "../../support/genie/GenieGhost.hpp"

#include <QCoreApplication>
#include <QLayout>
#include <QPainter>
#include <QToolBar>
#include <QToolButton>

namespace stencil::gui {

  namespace {
    // Hides a surface while its picture flies, in the surface's own background rather than the window's
    // behind it (an opacity veil showed white where the panel is grey), and tracks its sliding size.
    class FoldCover : public QWidget {
     public:
      FoldCover(QWidget* surface, QColor fill) : QWidget(surface), fill(fill) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        surface->installEventFilter(this);
        setGeometry(surface->rect());
        raise();
        show();
      }
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
        return false;
      }
      void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(rect(), fill); }
     private:
      QColor fill;
    };
    // `surface` as the window shows it: its own grab() paints its palette's base, not the grey drawn behind it.
    QPixmap windowShot(QWidget& win, const QWidget* surface) {
      return win.grab(QRect(surface->mapTo(&win, QPoint(0, 0)), surface->size()));
    }
    // The background the picture shows in its bottom-left corner, where no control sits.
    QWidget* coverOf(QWidget* surface, const QPixmap& shot) {
      const QImage px = shot.copy(std::min(3, shot.width() - 1), std::max(0, shot.height() - 4), 1, 1).toImage();
      return new FoldCover(surface, px.pixelColor(0, 0));
    }
    // Where `w` stands (GLOBAL), shown or not: a hidden toggle is placed before it shows.
    QRect globalRectOf(const QWidget* w) {
      if (!w || w->width() <= 0 || !w->parentWidget()) return QRect();
      return QRect(w->parentWidget()->mapToGlobal(w->pos()), w->size());
    }
    // A point at the middle of `edge` of `at` (GLOBAL), for a toggle folded out of sight.
    QRect edgePoint(const QRect& at, Qt::DockWidgetArea edge) {
      QRect p(0, 0, 24, 24);
      p.moveCenter(edge == Qt::LeftDockWidgetArea ? QPoint(at.left(), at.center().y())
                   : edge == Qt::TopDockWidgetArea ? QPoint(at.center().x(), at.top())
                   : edge == Qt::BottomDockWidgetArea ? QPoint(at.center().x(), at.bottom())
                                                      : QPoint(at.right(), at.center().y()));
      return p;
    }
  }  // namespace

  void DockChrome::settleFoldLayout() {
    for (int pass = 0; pass < 3; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      if (w.editor && w.editor->layout()) w.editor->layout()->activate();
      if (w.layout()) w.layout()->activate();
    }
  }

  void DockChrome::releaseBarsVeils() {
    for (const QPointer<QWidget>& cover : barsCovers) delete cover.data();
    barsCovers.clear();
  }

  bool DockChrome::pourPanel(bool show, int full, int ms) {
    if (!support::isSlideMotionOk() || !w.selPanel) return false;
    QWidget* panel = w.selPanel;
    if (show) { panel->setFixedWidth(full); settleFoldLayout(); }   // photographed as it will stand
    const QPixmap shot = windowShot(w, panel);
    const QRect at(panel->mapToGlobal(QPoint(0, 0)), panel->size());
    if (shot.isNull() || !at.isValid()) return false;
    const bool right = w.editor->dockWidgetArea(w.selPanel) != Qt::LeftDockWidgetArea;
    if (!show) positionPanelReopenButton();   // the chevron's landing, before it shows
    const auto home = [this, at, right] {
      const QRect r = globalRectOf(w.tools.panelReopenBtn);
      return r.isValid() ? r : edgePoint(at, right ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea);
    };
    panelCover = coverOf(panel, shot);   // in its place, unseen, while its picture flies
    const auto land = [this, show] { if (show) releasePanelVeil(); };
    return support::genie::fly(&w, shot, at, home(), show, /*asWindow=*/false, land, ms + support::genie::SPACE_TAIL_MS, home);
  }

  bool DockChrome::pourBars(const QList<QToolBar*>& bars, bool show, int ms) {
    if (!support::isSlideMotionOk() || bars.isEmpty()) return false;
    QRect local;
    for (QToolBar* b : bars)
      if (b->isVisible() && b->height() > 0) local |= QRect(b->mapTo(&w, QPoint(0, 0)), b->size());
    if (local.width() < 8 || local.height() < 8) return false;
    const QPixmap shot = w.grab(local);
    if (shot.isNull()) return false;
    const QRect at(w.mapToGlobal(local.topLeft()), local.size());
    releaseBarsVeils();
    for (QToolBar* b : bars) barsCovers.append(coverOf(b, shot));
    const auto home = [this, at] {
      const QRect r = w.tools.controlsPill && w.tools.controlsPill->isVisible() ? globalRectOf(w.tools.controlsPill) : QRect();
      return r.isValid() ? r : edgePoint(at, Qt::TopDockWidgetArea);
    };
    const auto land = [this, show] { if (show) releaseBarsVeils(); };
    return support::genie::fly(&w, shot, at, home(), show, /*asWindow=*/false, land, ms + support::genie::SPACE_TAIL_MS, home);
  }

  bool DockChrome::pourDockedChat(bool show, int ms, const std::function<void(int)>& pin, int full) {
    if (!support::isSlideMotionOk() || !w.chatDock) return false;
    ChatDock* dock = w.chatDock;
    const Qt::DockWidgetArea area = w.dockWidgetArea(dock);
    pin(full);   // photographed at the extent it opens to, or leaves from
    settleFoldLayout();
    const QPixmap shot = windowShot(w, dock);
    const QRect at(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    if (shot.isNull() || !at.isValid()) return false;
    const auto home = [this, area] {
      QWidget* icon = w.buttonForAction(w.acts.chat);
      return icon && icon->isVisible() ? globalRectOf(icon) : edgePoint(QRect(w.mapToGlobal(QPoint(0, 0)), w.size()), area);
    };
    chatCover = coverOf(dock, shot);   // the slide's end (stopChatAnim) hands it back
    const int run = ++chatPourRun;
    const auto land = [this, run, show] { if (show && run == chatPourRun) dropChatVeil(); };
    if (support::genie::fly(&w, shot, at, home(), show, /*asWindow=*/false, land, ms, home))
      return true;
    dropChatVeil();
    return false;
  }

}  // namespace stencil::gui

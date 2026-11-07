// The dust flights that ride the chat and panel extent slides, and the selected-line bar's; the chat's extent
// and space pins the slides drive, and the page gap they carry. Browser twin: ui/chat/panel/openState.js and
// ui/motion/control/chatFx.js for the flights.
#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "../../support/theme/faceSwap.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "ChatDock.hpp"
#include "theme.hpp"   // CHAT_PAGE_GAP_PX

#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPropertyAnimation>

#include <algorithm>

namespace stencil::gui {

  namespace {
    // Parented to the veil, so an interrupted flight takes the fade with it. Only the
    // selection bar uses it: a surface that SLIDES must hand over at the end instead.
    QPropertyAnimation* fadeVeilUp(QGraphicsOpacityEffect* veil, int ms) {
      auto* fade = new QPropertyAnimation(veil, "opacity", veil);
      holdFadeKeys(fade, ms);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
      return fade;
    }
  }  // namespace

  QPointer<gui::DisintegrateOverlay> DockChrome::chatSurfaceFlight(
      Qt::DockWidgetArea area, bool shouldGather, int ms, const std::function<void(int)>& pin, int full) {
    if (!w.chatDock) return nullptr;
    QWidget* dustHost = w.chatDock->window();
    if (!dustHost || !support::isDustMotionOk()) return nullptr;
    pin(full);
    // A dock is placed by the WINDOW's layout: without this the picture was measured at the
    // hidden dock's stale geometry, and the motes assembled a whole chat below where it lands.
    if (QLayout* l = w.layout()) l->activate();
    const QPixmap snap = w.chatDock->grab();
    const QRect picture(w.chatDock->mapTo(dustHost, QPoint(0, 0)), w.chatDock->size());
    pin(shouldGather ? 0 : full);   // gather starts empty; a scatter leaves from the settled size
    const QPoint target = dockAwayPoint(picture, area);
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, dustHost, target, shouldGather, ms, w.chatDock->palette().color(QPalette::WindowText));
    // The dock stays veiled for the WHOLE flight and stopChatAnim hands over at its end: it still slides while
    // the motes settle, and an early reveal showed the panel's text twice, one copy off the other.
    if (fx) {
      chatVeil = gui::veilBehindDust(w.chatDock);
      // The hand-over is the overlay's own end, never the slide's: their clocks start a beat apart, and that beat
      // drew neither. The dock is the context: a window tearing down deletes it first, and the hand-over goes too.
      QObject::connect(fx, &QObject::destroyed, w.chatDock, [this] { dropChatVeil(); });
    }
    return fx;
  }

  // The points panel's flight: photographed at its OPEN width whichever way the slide runs; the veil hides the real panel, so the motes ARE the panel.
  QPointer<gui::DisintegrateOverlay> DockChrome::panelSurfaceFlight(bool shouldGather, int ms, int full) {
    if (!w.selPanel) return nullptr;
    // Hosted by the editor shell, as every editor surface's dust is: the motes vanish under a
    // docked chat instead of crossing it (browser surfaces.js belowChat).
    QWidget* dustHost = w.editor;
    if (!dustHost || !support::isDustMotionOk()) return nullptr;
    w.selPanel->setFixedWidth(full);
    if (QLayout* l = w.editor->layout()) l->activate();   // the grab must see the open panel, not the rail
    // Children only, on a clear sheet: the page under the hugging card is no part of the panel.
    const qreal dpr = w.selPanel->devicePixelRatioF();
    QPixmap snap(w.selPanel->size() * dpr);
    snap.setDevicePixelRatio(dpr);
    snap.fill(Qt::transparent);
    w.selPanel->render(&snap, QPoint(), QRegion(), QWidget::DrawChildren);
    const QRect picture(w.selPanel->mapTo(dustHost, QPoint(0, 0)), w.selPanel->size());
    const Qt::DockWidgetArea area = w.editor->dockWidgetArea(w.selPanel) == Qt::LeftDockWidgetArea
                                        ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, dustHost, dockAwayPoint(picture, area), shouldGather, ms,
        w.selPanel->palette().color(QPalette::WindowText));
    if (fx) {
      // The rows folding beside it move the dock mid-flight: the picture rides along, so the
      // hand-over lands where the motes gathered instead of a band above the real panel.
      fx->setFollow(w.selPanel);
      panelVeil = gui::veilBehindDust(w.selPanel);
    }
    return fx;
  }

  // Where the bar's motes land. `isClosing` predicts imageInfoBar's post-close position — dustSelectedLineBarOut() runs before the dock hides.
  QPoint DockChrome::selectedLineBarDustPoint(const QRect& barPicture, bool isClosing) {
    if (w.tools.imageInfoBar && w.tools.imageInfoBar->isVisible()) {
      const QRect infoRect(w.tools.imageInfoBar->mapTo(w.editor, QPoint(0, 0)), w.tools.imageInfoBar->size());
      if (infoRect.width() >= 8 && infoRect.height() >= 8) {
        const int dockTop = w.selectedLineDock ? w.selectedLineDock->mapTo(w.editor, QPoint(0, 0)).y()
                                              : barPicture.top();
        const int y = isClosing ? dockTop + infoRect.height() : infoRect.bottom();
        return QPoint(barPicture.center().x(), y);
      }
    }
    return dockAwayPoint(barPicture, Qt::BottomDockWidgetArea);
  }

  // Entrance (browser selectionPanel.js surfaceIn). Call AFTER setVisible(true) + layout()->activate(), so the grab sees the populated bar.
  void DockChrome::dustSelectedLineBarIn() {
    if (!w.selectedLineBar) return;
    if (!support::isDustMotionOk()) return;
    const QPixmap snap = w.selectedLineBar->grab();
    const QRect picture(w.selectedLineBar->mapTo(w.editor, QPoint(0, 0)), w.selectedLineBar->size());
    auto* fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, w.editor, selectedLineBarDustPoint(picture, /*isClosing=*/false), /*gather=*/true, 0,
        w.palette().color(QPalette::WindowText));
    if (!fx) return;
    // The veil hides the real bar for the gather; released once landed.
    QGraphicsOpacityEffect* veil = gui::veilBehindDust(w.selectedLineBar);
    auto* fade = fadeVeilUp(veil, gui::DisintegrateOverlay::SURFACE_IN_MS);
    QPointer<SelectedLineBar> bar = w.selectedLineBar;
    QObject::connect(fade, &QPropertyAnimation::finished, &w, [bar, veil] {
      if (bar && bar->graphicsEffect() == veil) bar->setGraphicsEffect(nullptr);
    });
  }

  // Exit (surfaceOut). Call BEFORE setVisible(false), so the snapshot still shows the real bar.
  void DockChrome::dustSelectedLineBarOut() {
    if (!w.selectedLineBar) return;
    if (!support::isDustMotionOk()) return;
    const QPixmap snap = w.selectedLineBar->grab();
    const QRect picture(w.selectedLineBar->mapTo(w.editor, QPoint(0, 0)), w.selectedLineBar->size());
    gui::DisintegrateOverlay::overSurface(
        snap, picture, w.editor, selectedLineBarDustPoint(picture, /*isClosing=*/true), /*gather=*/false, 0,
        w.palette().color(QPalette::WindowText));
  }

  std::function<void(int)> DockChrome::chatExtentPin(bool isHorizontal) {
    return [this, isHorizontal](int v) {
      if (isHorizontal) w.chatDock->setFixedWidth(v);
      else w.chatDock->setFixedHeight(v);
    };
  }

  std::function<void(int)> DockChrome::chatSpacePin(Qt::DockWidgetArea area) {
    const auto pin = chatExtentPin(area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea);
    return [this, pin](int space) { pin(chatGapless ? space : std::max(0, space - CHAT_PAGE_GAP_PX)); };
  }

  // The window's own separator, 0 while a covered chat slide carries the page gap in its extent (shell.qss
  // [chatGapless]): the space opens from 0 and shuts to 0 with no 10 px snap, as the browser's one padding.
  void DockChrome::setChatGapless(bool isOn) {
    if (chatGapless == isOn) return;
    chatGapless = isOn;
    w.setProperty("chatGapless", isOn);
    gui::repolish(&w, /*styleChange=*/true);   // QMainWindow re-reads its separator extent
  }
}  // namespace stencil::gui

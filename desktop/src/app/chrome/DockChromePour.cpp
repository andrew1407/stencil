// Sliding's picture for the side panel, the toolbar rows and the docked chat: it pours out of the toggle that
// opened them and back into it (support/genie) while their place slides open or shut on the same clock.
// Browser twin: ui/motion/control/tips.js pourFold and ui/chat/panel/openState.js.
#include "MainWindow.hpp"
#include "DockChrome.hpp"
#include "ChatDock.hpp"
#include "SelectionPanel.hpp"
#include "../../support/genie/GenieGhost.hpp"
#include "../../support/genie/SurfaceCover.hpp"

#include <QCoreApplication>
#include <QCursor>
#include <QHash>
#include <QImage>
#include <QLayout>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <array>

namespace stencil::gui {

  namespace {
    // `surface` as the window shows it: its own grab() paints its palette's base, not the grey drawn behind it.
    QPixmap windowShot(QWidget& win, const QWidget* surface) {
      return win.grab(QRect(surface->mapTo(&win, QPoint(0, 0)), surface->size()));
    }
    // The background the picture shows in its bottom-left corner, where no control sits.
    QWidget* coverOf(QWidget* surface, const QPixmap& shot) {
      const QImage px = shot.copy(std::min(3, shot.width() - 1), std::max(0, shot.height() - 4), 1, 1).toImage();
      return support::genie::fillCover(surface, px.pixelColor(0, 0));
    }
    // The fullscreen canvas ground: the commonest colour down the viewport's right edge, inside its hairline;
    // one pixel read the chevron or a hint that sits over the ground there.
    QColor groundOf(QWidget& win, const QWidget* view) {
      const QPoint at = view->mapTo(&win, QPoint(std::max(0, view->width() - 9), 0));
      const QImage col = win.grab(QRect(at, QSize(1, view->height()))).toImage();
      QHash<QRgb, int> seen;
      QRgb best = 0;
      for (int y = 0; y < col.height(); ++y)
        if (++seen[col.pixel(0, y)] > seen.value(best)) best = col.pixel(0, y);
      return QColor::fromRgb(best);
    }
    // Where `w` stands (GLOBAL), shown or not: a hidden toggle is placed before it shows.
    QRect globalRectOf(const QWidget* w) {
      if (!w || w->width() <= 0 || !w->parentWidget()) return QRect();
      return QRect(w->parentWidget()->mapToGlobal(w->pos()), w->size());
    }
    // The four edges in one order, the key of the tables below; an unknown area reads as the right edge.
    constexpr std::array<Qt::DockWidgetArea, 4> EDGES{Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea,
                                                      Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea};
    int edgeIndex(Qt::DockWidgetArea edge) {
      const auto it = std::find(EDGES.begin(), EDGES.end(), edge);
      return it == EDGES.end() ? 3 : static_cast<int>(it - EDGES.begin());
    }
    using EdgeMid = QPoint (*)(const QRect&);
    constexpr EdgeMid EDGE_MIDS[] = {[](const QRect& r) { return QPoint(r.center().x(), r.top()); },
                                     [](const QRect& r) { return QPoint(r.center().x(), r.bottom()); },
                                     [](const QRect& r) { return QPoint(r.left(), r.center().y()); },
                                     [](const QRect& r) { return QPoint(r.right(), r.center().y()); }};
    // A 24×2 strip just past the edge at `v` along it, so the picture flows straight out through it, never up a corner.
    using EdgeStrip = QRect (*)(const QRect&, int);
    constexpr EdgeStrip EDGE_STRIPS[] = {[](const QRect& r, int v) { return QRect(v - 12, r.top() - 2, 24, 2); },
                                         [](const QRect& r, int v) { return QRect(v - 12, r.bottom() + 1, 24, 2); },
                                         [](const QRect& r, int v) { return QRect(r.left() - 2, v - 12, 2, 24); },
                                         [](const QRect& r, int v) { return QRect(r.right() + 1, v - 12, 2, 24); }};

    // A point at the middle of `edge` of `at` (GLOBAL), for a toggle folded out of sight.
    QRect edgePoint(const QRect& at, Qt::DockWidgetArea edge) {
      QRect p(0, 0, 24, 24);
      p.moveCenter(EDGE_MIDS[edgeIndex(edge)](at));
      return p;
    }
    // edgePoint where the pointer crosses that edge: fullscreen's reveals pour from the pointer, the
    // top strip at its x and a side list at its y (browser fullscreen/panels.js edgeHome).
    QRect pointerOnEdge(const QRect& at, Qt::DockWidgetArea edge) {
      const QPoint c = QCursor::pos();
      const int k = edgeIndex(edge);
      const bool isAcross = k < 2;
      const int lo = isAcross ? at.left() + 12 : at.top() + 12, hi = isAcross ? at.right() - 12 : at.bottom() - 12;
      return EDGE_STRIPS[k](at, std::clamp(isAcross ? c.x() : c.y(), lo, std::max(lo, hi)));
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

  bool DockChrome::pourPanel(bool shouldShow, int full, int ms) {
    if (!support::isSlideMotionOk() || !w.selPanel) return false;
    QWidget* panel = w.selPanel;
    if (shouldShow) { panel->setFixedWidth(full); settleFoldLayout(); }   // photographed as it will stand
    const QPixmap shot = windowShot(w, panel);
    const QRect at(panel->mapToGlobal(QPoint(0, 0)), panel->size());
    if (shot.isNull() || !at.isValid()) return false;
    const bool isRight = w.editor->dockWidgetArea(w.selPanel) != Qt::LeftDockWidgetArea;
    if (!shouldShow) positionPanelReopenButton();   // the chevron's landing, before it shows
    const Qt::DockWidgetArea edge = isRight ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
    const QRect crossed = w.fs.active ? pointerOnEdge(at, edge) : QRect();   // read once: the landing never chases the pointer
    // A strip just past the panel's outer edge, level with the chevron (browser panel/coordFold.js edgeHome):
    // the column folds sideways into it, never at the chevron itself, which sits over the panel.
    const auto home = [this, at, isRight, crossed] {
      if (crossed.isValid()) return crossed;
      const QRect r = globalRectOf(w.tools.panelReopenBtn);
      const int y = r.isValid() ? r.top() : at.top(), h = r.isValid() ? std::max(2, r.height()) : 24;
      return QRect(isRight ? at.right() + 1 : at.left() - 2, y, 2, h);
    };
    // In fullscreen the panel hangs over the canvas ground: its opening space wears that ground, so only the picture flies.
    panelCover = w.fs.active && w.scroll ? support::genie::fillCover(panel, groundOf(w, w.scroll->viewport())) : coverOf(panel, shot);
    // Under its cover the panel is unseen: it stops repainting under each frame of the picture (30 a flight), back on landing.
    for (QWidget* part : {w.selPanel->titleBarWidget(), w.selPanel->widget()}) if (part) part->setUpdatesEnabled(false);
    const auto land = [this, shouldShow] { if (shouldShow) releasePanelVeil(); };
    return support::genie::fly(&w, shot, at, home(), shouldShow, /*asWindow=*/false, land, ms + support::genie::SPACE_TAIL_MS);
  }

  bool DockChrome::pourBars(const QList<QToolBar*>& bars, bool shouldShow, int ms) {
    if (!support::isSlideMotionOk() || bars.isEmpty()) return false;
    QRect local;
    for (QToolBar* b : bars)
      if (b->isVisible() && b->height() > 0) local |= QRect(b->mapTo(&w, QPoint(0, 0)), b->size());
    if (local.width() < 8 || local.height() < 8) return false;
    const QPixmap shot = w.grab(local);
    if (shot.isNull()) return false;
    const QRect at(w.mapToGlobal(local.topLeft()), local.size());
    releaseBarsVeils();
    const QColor ground = w.fs.active && w.scroll ? groundOf(w, w.scroll->viewport()) : QColor();
    for (QToolBar* b : bars)   // fullscreen: the ground the rows hang over, as the panel's cover
      barsCovers.append(ground.isValid() ? support::genie::fillCover(b, ground) : coverOf(b, shot));
    const QRect crossed = w.fs.active ? pointerOnEdge(at, Qt::TopDockWidgetArea) : QRect();
    const auto home = [this, at, crossed] {
      if (crossed.isValid()) return crossed;
      const QRect r = w.tools.controlsPill && w.tools.controlsPill->isVisible() ? globalRectOf(w.tools.controlsPill) : QRect();
      return r.isValid() ? r : edgePoint(at, Qt::TopDockWidgetArea);
    };
    const auto land = [this, shouldShow] { if (shouldShow) releaseBarsVeils(); };
    return support::genie::fly(&w, shot, at, home(), shouldShow, /*asWindow=*/false, land, ms + support::genie::SPACE_TAIL_MS);
  }

  bool DockChrome::pourDockedChat(bool shouldShow, int ms, const std::function<void(int)>& pin, int full, int shut) {
    if (!support::isSlideMotionOk() || !w.chatDock) return false;
    ChatDock* dock = w.chatDock;
    const Qt::DockWidgetArea area = w.dockWidgetArea(dock);
    // The icon read once, where it stands with the space shut (browser genieIn / landingOf): re-read, it rode the slide.
    pin(shut);
    settleFoldLayout();
    QWidget* icon = w.buttonForAction(w.acts.chat);
    const QRect home = icon && icon->isVisible() ? globalRectOf(icon) : edgePoint(QRect(w.mapToGlobal(QPoint(0, 0)), w.size()), area);
    pin(full);   // photographed at the extent it opens to, or leaves from
    settleFoldLayout();
    const QPixmap shot = windowShot(w, dock);
    const QRect at(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    if (shot.isNull() || !at.isValid()) return false;
    freezeChatBody(area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea);   // at the photographed extent
    // An open goes back to its shut extent before the ghost maps: mapping a window flushes this one, and blinked the open layout.
    if (shouldShow) {
      pin(shut);
      settleFoldLayout();
    }
    chatCover = groundCover(dock);   // the slide's end (stopChatAnim) hands it back
    const int run = ++chatPourRun;
    const auto land = [this, run, shouldShow] { if (shouldShow && run == chatPourRun) dropChatVeil(); };
    if (support::genie::fly(&w, shot, at, home, shouldShow, /*asWindow=*/true, land, ms)) return true;
    dropChatVeil();
    return false;
  }

  // Under its cover the chat's body holds its photographed extent (the dock clips it) and paints nothing: no re-wrap a frame.
  void DockChrome::freezeChatBody(bool isHorizontal) {
    QWidget* body = w.chatDock->widget();
    if (!body) return;
    chatFrozenBody = body;
    if (isHorizontal) body->setFixedWidth(body->width());
    else body->setFixedHeight(body->height());
    for (QWidget* part : {w.chatDock->titleBarWidget(), body}) if (part) part->setUpdatesEnabled(false);
  }

  void DockChrome::thawChatBody() {
    QWidget* body = chatFrozenBody.data();
    chatFrozenBody = nullptr;
    if (!body) return;
    body->setMinimumSize(0, 0);
    body->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    for (QWidget* part : {w.chatDock ? w.chatDock->titleBarWidget() : nullptr, body}) if (part) part->setUpdatesEnabled(true);
  }

}  // namespace stencil::gui

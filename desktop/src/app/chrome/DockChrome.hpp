#pragma once
#include <QPointer>
#include <QRect>
#include <QSize>
#include <functional>

class QGraphicsOpacityEffect;
class QVariantAnimation;
class QWidget;

class QToolBar;

namespace stencil::gui {

  class MainWindow;
  class DisintegrateOverlay;
  class DockEdgeOverlay;

  // The docks around the editor and how they move: the chat dock's placement, slide, float and
  // compact popover, the points panel's slide and its grip, the selected-line bar's dust, and
  // the resize edges and toast inset that follow them.
  class DockChrome {
   public:
    explicit DockChrome(MainWindow& w) : w(w) {}

    // Browser parity: each chevron lives where its menu is, NOT in the top toolbar.
    void buildOverlayArrows();
    void syncToastInset();
    void updatePanelReopenButton();
    void positionPanelReopenButton();
    // centralLayout's right inset: the panel-open gap, the reopen chevron's band, or a slide between them.
    void setCanvasRightInset(int right);
    void positionPanelGrip();
    void positionChatEdge();
    // Browser mainContent.js surface flight; panelVeil hides the panel so the motes ARE it.
    QPointer<gui::DisintegrateOverlay> panelSurfaceFlight(bool gather, int ms, int full);
    void releasePanelVeil();
    // Browser selectionPanel.js surfaceIn/Out.
    void dustSelectedLineBarIn();
    void dustSelectedLineBarOut();
    QPoint selectedLineBarDustPoint(const QRect& barPicture, bool closing);
    void setChatShown(bool show, bool animate);
    void dockChatTo(Qt::DockWidgetArea area);
    QPointer<gui::DisintegrateOverlay> chatSurfaceFlight(Qt::DockWidgetArea area, bool gather, int ms,
                                                         const std::function<void(int)>& pin, int full);
    // `revealFrom` (GLOBAL): where the float grows out of in place of the chat icon (a drop's cursor).
    void toggleChatFloat(const QRect& revealFrom = QRect());
    void openChatCompactNow(QWidget* anchor);
    QRect compactChatRect(QWidget* anchor) const;
    void dropChatVeil();
    void stopChatAnim();
    // Sliding (DockChromePour.cpp): the docked chat, the side panel and the toolbar rows pour their picture
    // out of their toggle and back while their space slides on the same `ms`; false = not Sliding, dust instead.
    bool pourDockedChat(bool show, int ms, const std::function<void(int)>& pin, int full);
    bool pourPanel(bool show, int full, int ms);
    bool pourBars(const QList<QToolBar*>& bars, bool show, int ms);
    void releaseBarsVeils();
    void settleFoldLayout();
    void setChatCompactPopover(bool on);
    // Browser FLOAT_DEFAULT, ported to this window's top-left; clamped to its own screen.
    QRect defaultChatFloatRect() const;
    // The edge bands a chat drag drops on, up until `stillDragging` turns false.
    void showChatDockZones(std::function<bool()> stillDragging);
    // Opened on `area`'s edge, or slid there from wherever it shows.
    void openChatDocked(Qt::DockWidgetArea area);
    // Opened or moved as a float with its top-left on `global` (GLOBAL), kept on the window's screen.
    void openChatFloatingAt(const QPoint& global);

    // The canvas ↔ panel handle (browser .panel-resizer): it takes the pointer, so it wears the resize cursor.
    DockEdgeOverlay* panelGrip = nullptr;
    int panelGripStart = 0;   // the panel's width when the handle was grabbed
    // The chat's resize handle, a strip inside the dock's own edge (browser .chat-resizer).
    DockEdgeOverlay* chatEdge = nullptr;
    int chatEdgeStart = 0;   // the dock's extent when the handle was grabbed
    QVariantAnimation* chatAnim = nullptr;
    int chatRestoreExtent = 0;
    QSize chatNaturalMin;
    QPointer<QGraphicsOpacityEffect> chatVeil;
    QPointer<QGraphicsOpacityEffect> panelVeil;
    QPointer<QWidget> chatCover, panelCover;   // Sliding's covers (DockChromePour.cpp), dropped with the veils
    QList<QPointer<QWidget>> barsCovers;
    // Always through setChatCompactPopover: the dock must hear it too, or it stays undraggable.
    bool chatCompactPopover = false;
    bool chatClosing = false;
    int chatPourRun = 0;   // the pour in the air; a later toggle retires it before it lands
    Qt::DockWidgetArea chatCompactPrevArea = Qt::LeftDockWidgetArea;
    QRect chatFloatRect;
    QRect chatRevealFrom;   // GLOBAL: set only while a drop opens the float, which grows out of it

    std::function<void(int)> chatExtentPin(bool horiz);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui

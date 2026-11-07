#pragma once
// The docks around the editor and how they move: the chat dock's placement, slide, float and compact popover,
// the points panel's slide and its grip, the selected-line bar's dust, and the resize edges and toast inset
// that follow them. Browser twin: ui/chat/panel/ and ui/panel/ (the coordinate panel's fold).
#include <QPointer>
#include <QRect>
#include <QSize>
#include <functional>

class QGraphicsOpacityEffect;
class QToolBar;
class QVariantAnimation;
class QWidget;

namespace stencil::gui {

  class MainWindow;
  class DisintegrateOverlay;
  class DockEdgeOverlay;

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
    QPointer<gui::DisintegrateOverlay> panelSurfaceFlight(bool shouldGather, int ms, int full);
    void releasePanelVeil();
    // Browser selectionPanel.js surfaceIn/Out.
    void dustSelectedLineBarIn();
    void dustSelectedLineBarOut();
    QPoint selectedLineBarDustPoint(const QRect& barPicture, bool isClosing);
    void setChatShown(bool shouldShow, bool shouldAnimate);
    void flyFloatingChat(bool shouldShow);
    void dockChatTo(Qt::DockWidgetArea area);
    QPointer<gui::DisintegrateOverlay> chatSurfaceFlight(Qt::DockWidgetArea area, bool shouldGather, int ms,
                                                         const std::function<void(int)>& pin, int full);
    // `revealFrom` (GLOBAL): where the float grows out of in place of the chat icon (a drop's cursor).
    void toggleChatFloat(const QRect& revealFrom = QRect());
    void openChatCompactNow(QWidget* anchor);
    QRect compactChatRect(QWidget* anchor) const;
    void dropChatVeil();
    void stopChatAnim();
    // Sliding: the docked chat, the side panel and the toolbar rows pour their picture out of their toggle and
    // back while their space slides on the same `ms`; false = not Sliding. `shut`: the chat's closed extent.
    bool pourDockedChat(bool shouldShow, int ms, const std::function<void(int)>& pin, int full, int shut);
    // Transition: the chat's open picture held to its edge while its space slides; false = not Transition.
    bool revealDockedChat(bool shouldShow, const std::function<void(int)>& pin, int full);
    void pourChatMove(Qt::DockWidgetArea area, const std::function<void()>& place);
    // A cover for `surface` in the window's own background: what a pouring chat's space shows.
    QWidget* groundCover(QWidget* surface);
    bool pourPanel(bool shouldShow, int full, int ms);
    bool pourBars(const QList<QToolBar*>& bars, bool shouldShow, int ms);
    void releaseBarsVeils();
    void freezeChatBody(bool isHorizontal);
    void thawChatBody();
    void settleFoldLayout();
    void setChatCompactPopover(bool isOn);
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
    QPointer<QWidget> chatFrozenBody;   // the chat's body held at its photographed extent under chatCover
    // Always through setChatCompactPopover: the dock must hear it too, or it stays undraggable.
    bool chatCompactPopover = false;
    bool chatClosing = false;
    bool chatGapless = false;   // setChatGapless: the window's separator at 0, the gap inside the chat's extent
    int chatPourRun = 0;   // the pour in the air; a later toggle retires it before it lands
    Qt::DockWidgetArea chatCompactPrevArea = Qt::LeftDockWidgetArea;
    QRect chatFloatRect;
    QRect chatRevealFrom;   // GLOBAL: set only while a drop opens the float, which grows out of it

    std::function<void(int)> chatExtentPin(bool isHorizontal);
    // The chat's whole space on `area`, its dock and the page gap beside it, as one length (px).
    std::function<void(int)> chatSpacePin(Qt::DockWidgetArea area);
    void setChatGapless(bool isOn);

    void settleChatHover();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui

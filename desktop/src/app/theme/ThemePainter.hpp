#pragma once
#include <QColor>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QAction;
class QToolButton;
class QMenu;

namespace stencil::gui {

  class MainWindow;

  // Paints the chosen look onto the window's own chrome: the accent preview and pick, the action
  // and toolbar icons, the danger and toggle faces, the image-info line, and the webcore skin.
  class ThemePainter {
   public:
    explicit ThemePainter(MainWindow& w) : w(w) {}

    bool toggleWebcore();
    void unpinFaceWidths();
    void webcoreScene();
    void syncImageInfoDockHeight();
    QPixmap makeLogoPixmap(int size) const;
    void openAccentPicker();
    void remarkAccentPopover();
    // Browser twin accentController.previewAccent: instant repaint, no wipe, no persist.
    void previewAccent(const QString& key);
    void endAccentPreview();
    void commitAccent(const QString& key);   // a pick: apply + persist; the popover is the caller's
    void sizeViewToggles();
    void refreshDropHint();
    void restyleImageSizeInfo();
    void styleActionIcons(bool dark, const QColor& iconColor);
    void styleDangerToolButtons();
    void syncDrawToggleFace(bool drawing, bool animate);
    void syncDrawModeFace(bool rect, bool animate);
    // macOS menu-bar icons follow the SYSTEM appearance, not our theme.
    void retintMenuIconsForSystem(bool appDark, const QColor& appIconColor);
    // True while that retint is in force: the shared actions wear the system's ink, not the app's.
    bool menuIconsRetinted() const;
    // A menu drawn in the app's theme: its shared actions take the app's ink until restoreMenuInk().
    void appInkFor(QMenu& menu);
    void restoreMenuInk();
    void restyleContextToggles(const QColor& textColor);

    QString accentPreviewSaved;
    bool accentPreviewActive = false;

    void toggleTheme();
    // The theme applyTheme paints: a forced one (a skin's, or the lens's photograph), else the
    // stored mode resolved.
    bool paintingDark() const;
    // Dragged, the theme switch is a lens previewing the other theme (ThemePainterLens.cpp).
    void installThemeLens();
    // The window as the other theme paints it, the theme on screen left as it is.
    QPixmap otherThemeShot();

   private:
    void openLens(const QPoint& global);
    // `done` runs once the circle has closed (at once with no lens up).
    void closeLens(std::function<void()> done = {});
    bool lensAllowed() const;
    QString lensKey() const;
    void primeLens();

    MainWindow& w;
    QPointer<QWidget> lens;
    QPixmap primed;      // the other theme photographed at idle, ahead of a drag
    QString primedKey;   // the theme, accent and size it was taken in
  };

}  // namespace stencil::gui

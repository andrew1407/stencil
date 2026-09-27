#pragma once
#include <QColor>
#include <QPointer>
#include <QString>
#include <QWidget>

class QAction;
class QPixmap;
class QToolButton;

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
    void restyleContextToggles(const QColor& textColor);

    QString accentPreviewSaved;
    bool accentPreviewActive = false;

    void toggleTheme();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui

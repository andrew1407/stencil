#pragma once
#include <QString>
#include <QWidget>

class QEvent;
class QObject;
class QPaintEvent;

// Incognito indicator overlay. Port of the browser's body.incognito-mode styling
// (browser/css/components.css): a 3px dashed accent outline inset around the canvas
// VIEWPORT plus a "🕶 Incognito — not saved" pill badge pinned to the top-left
// corner. Like the browser, it tracks the visible frame, NOT the image — so it
// stays put as the image pans/zooms, and frames the whole viewport even when the
// image is smaller than it. Purely visual; saving is gated in MainWindow.
//
// Implemented as a transparent, click-through child of the scroll viewport (the
// desktop analog of .canvas-viewport), raised above the canvas. It resizes itself
// to fill the viewport by watching the parent's resize events (cf. Notifications).
namespace stencil::gui {

  class IncognitoOverlay : public QWidget {
    Q_OBJECT
   public:
    explicit IncognitoOverlay(QWidget* viewport);

    // Show/hide the indicator (driven by MainWindow's incognito_ state).
    void setActive(bool on);
    // Recolour to the current theme accent (cf. CanvasWidget::setAccent/setDark).
    void setTheme(bool dark, const QString& accentKey);

   protected:
    void paintEvent(QPaintEvent* event) override;
    // Track the parent viewport's size so the outline always frames it.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void fitToParent();

    bool active_ = false;
    bool dark_ = false;
    QString accentKey_ = "violet";
  };

}

#pragma once
#include <QFrame>
#include <QPoint>
#include <QString>
#include <utility>
#include <vector>

class QLabel;
class QVariantAnimation;

// Floating, frameless tooltip shown over the canvas on hover. Port of
// browser/js/ui/tooltip.js: it renders rows (label -> value pairs) and positions
// itself near the cursor, flipping to stay on screen. The decision of WHAT to
// show (cursor coords / nearest point / line endpoints) is made by MainWindow,
// mirroring tooltip.js applyHover; this widget only renders + positions.
namespace stencil::gui {

  class CanvasTooltip : public QFrame {
    Q_OBJECT
   public:
    explicit CanvasTooltip(QWidget* parent = nullptr);

    // Replace the displayed rows. Empty -> dust/fade out and hide.
    void setRows(const std::vector<std::pair<QString, QString>>& rows);

    // Show near a global cursor position, flipping to stay on screen (port of
    // tooltip.js position(): offset +15, flip when overflowing, clamp to >= 10).
    void showAt(const QPoint& globalCursor);

   private:
    // Fade/dust out then hide (idempotent). The way back in lives in showAt().
    void hideTip();
    // Fly the tip's own motes out of / back into the point it appeared at. Measured in the top-level
    // window's coords so escapeHost can carry the cloud past it. False -> the caller plain-fades.
    bool dust(bool gather);

    QLabel* body = nullptr;
    QVariantAnimation* fade = nullptr;
    QPoint lastCursor;      // where the dust flies out of / back into
    bool closing = false;   // fade is mid fade-OUT; its finished handler should hide()
  };

}

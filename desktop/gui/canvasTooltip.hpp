#pragma once
#include <QFrame>
#include <QString>
#include <utility>
#include <vector>

class QLabel;

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

    // Replace the displayed rows. Empty -> hidden.
    void setRows(const std::vector<std::pair<QString, QString>>& rows);

    // Show near a global cursor position, flipping to stay on screen (port of
    // tooltip.js position(): offset +15, flip when overflowing, clamp to >= 10).
    void showAt(const QPoint& globalCursor);

   private:
    QLabel* body_ = nullptr;
  };

}

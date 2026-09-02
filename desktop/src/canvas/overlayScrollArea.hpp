#pragma once
// True overlay scrollbars for the canvas viewport.
// Browser parity: layout.css's .canvas-viewport scrollbar is a macOS-style
// overlay bar with zero reserved layout space (see layout.css:57-58). Qt's
// native transient mode matches that, but theme.cpp's QScrollBar stylesheet
// turns it off app-wide, so this widget redoes both axes' range/geometry
// against the full frame (not the base class's shrunk viewport) and floats
// the bars back on top.
#include <QEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <algorithm>

namespace stencil::gui {

  class OverlayScrollArea : public QScrollArea {
   public:
    using QScrollArea::QScrollArea;

    // A pure zoom (canvas_->resize()) doesn't reliably fire a Resize/LayoutRequest event, so
    // MainWindow calls this directly from revealCanvasScrollbars(). Public so mainWindow.cpp
    // can reach it through the QScrollArea* it stores scroll_ as.
    void relayout() { overlayBars(); }

   protected:
    // Resize/LayoutRequest are a backstop for relayout() above, not the primary trigger.
    bool event(QEvent* e) override {
      const bool handled = QScrollArea::event(e);
      if (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest ||
          e->type() == QEvent::Show)
        overlayBars();
      return handled;
    }

   private:
    // Qt wraps each bar in a private container widget (a child of `this`); the base class's
    // shrunk-viewport layout leaves that container hidden/misplaced after a bare zoom, so
    // drive its visibility/geometry directly here rather than trusting the base class. The
    // bar itself is positioned at 0,0 local — relative to the container, not to `this`.
    void positionBar(QScrollBar* bar, Qt::ScrollBarPolicy policy, int overflow, bool vertical,
                      const QRect& full) {
      if (!bar) return;
      bar->setRange(0, std::max(0, overflow));
      const bool want =
          policy == Qt::ScrollBarAlwaysOn || (policy != Qt::ScrollBarAlwaysOff && overflow > 0);
      bar->setVisible(want);
      QWidget* container = bar->parentWidget();
      if (!container) return;
      container->setVisible(want);
      container->raise();
      if (vertical) {
        const int vw = bar->sizeHint().width();
        container->setGeometry(full.right() - vw + 1, full.top(), vw, full.height());
        bar->setGeometry(0, 0, vw, full.height());
      } else {
        const int hh = bar->sizeHint().height();
        container->setGeometry(full.left(), full.bottom() - hh + 1, full.width(), hh);
        bar->setGeometry(0, 0, full.width(), hh);
      }
    }

    void overlayBars() {
      QWidget* w = widget();
      if (!w || !viewport()) return;
      const QRect full = contentsRect();
      if (viewport()->geometry() != full) viewport()->setGeometry(full);
      positionBar(verticalScrollBar(), verticalScrollBarPolicy(), w->height() - full.height(),
                  true, full);
      positionBar(horizontalScrollBar(), horizontalScrollBarPolicy(), w->width() - full.width(),
                  false, full);
    }
  };

}  // namespace stencil::gui

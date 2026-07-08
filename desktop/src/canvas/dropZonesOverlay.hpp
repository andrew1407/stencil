#pragma once
// Split image-drop overlay: LEFT half = "Upload & save", RIGHT half = "Upload incognito"
// (the desktop analogue of the browser's #global-drop-overlay split — feature #3). Shown
// while a droppable file is dragged over the window; the half under the cursor highlights.
// Transparent + click-through (so drops still reach the MainWindow), and resizes to its
// parent viewport by watching its resizes (like IncognitoOverlay). Purely visual — the
// actual save-vs-incognito / here-vs-new-window decision is made in MainWindow::dropEvent.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QList>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <cmath>

namespace stencil::gui {

  class DropZonesOverlay : public QWidget {
   public:
    explicit DropZonesOverlay(QWidget* viewport) : QWidget(viewport) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
      // Drive the pulsing zone glyph: advance the phase + repaint ~60fps while shown so the pulse
      // reads as smooth (step matched to the interval for a ~1.5s breathing cycle).
      pulse_.setInterval(16);
      QObject::connect(&pulse_, &QTimer::timeout, [this] {
        phase_ += 0.067;
        if (phase_ > 6.2831853) phase_ -= 6.2831853;
        update();
      });
      if (viewport) { viewport->installEventFilter(this); fitToParent(); }
    }

    void setAccent(const QColor& c) { accent_ = c; update(); }
    void setActiveLeft(bool left) { if (activeLeft_ != left) { activeLeft_ = left; update(); } }
    void showZones() { fitToParent(); raise(); show(); pulse_.start(); }
    void hideZones() { pulse_.stop(); hide(); }

   protected:
    bool eventFilter(QObject* w, QEvent* e) override {
      if (w == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::Move)) fitToParent();
      return QWidget::eventFilter(w, e);
    }
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      const int w = width(), h = height();
      const QRect left(10, 10, w / 2 - 15, h - 20);
      const QRect right(w / 2 + 5, 10, w / 2 - 15, h - 20);
      drawZone(p, left, accent_, QStringLiteral("↑"), QStringLiteral("Upload & save"),
               QStringLiteral("Load the image and keep it in your projects"), activeLeft_);
      drawZone(p, right, muted_, QStringLiteral("◐"), QStringLiteral("Upload incognito"),
               QStringLiteral("Load the image without saving it"), !activeLeft_);
    }

   private:
    void fitToParent() { if (parentWidget()) setGeometry(parentWidget()->rect()); }
    void drawZone(QPainter& p, const QRect& r, const QColor& col, const QString& glyph,
                  const QString& title, const QString& sub, bool active) {
      const QFont base = p.font();
      // Mostly-opaque fill with a slight see-through, blended toward a dark panel so the zones
      // read clearly over the canvas without fully hiding it.
      QColor panel(30, 30, 34);
      QColor fill(
          (col.red() * (active ? 45 : 32) + panel.red() * (100 - (active ? 45 : 32))) / 100,
          (col.green() * (active ? 45 : 32) + panel.green() * (100 - (active ? 45 : 32))) / 100,
          (col.blue() * (active ? 45 : 32) + panel.blue() * (100 - (active ? 45 : 32))) / 100);
      fill.setAlpha(active ? 225 : 200);  // ≈0.88 / 0.78 — a little transparency
      p.fillRect(r, fill);
      // Thin DASHED border — short dashes (pattern in units of line width).
      QPen pen(col);
      pen.setWidth(3);
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern(QList<qreal>{3.0, 2.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r, 14, 14);
      // Pulsing glyph (small → large → small) to catch the eye on drag. Float point size → the
      // pulse is already sub-pixel smooth; the ~60fps timer keeps the motion even.
      const double scale = 1.0 + 0.18 * std::sin(phase_);
      p.setPen(col);
      QFont gf = base;
      gf.setPointSizeF(34.0 * scale);
      gf.setBold(true);
      p.setFont(gf);
      p.drawText(QRect(r.left(), r.top() + r.height() / 6, r.width(), r.height() / 3),
                 Qt::AlignHCenter | Qt::AlignVCenter, glyph);
      // Title.
      QFont f = base;
      f.setPointSizeF(base.pointSizeF() + 5);
      f.setBold(true);
      p.setFont(f);
      p.drawText(QRect(r.left() + 10, r.center().y() - r.height() / 12, r.width() - 20, r.height() / 4),
                 Qt::AlignHCenter | Qt::AlignVCenter, title);
      // Subtitle.
      QFont sf = base;
      sf.setPointSizeF(base.pointSizeF() - 1);
      p.setFont(sf);
      p.setPen(QColor("#c4c8cc"));
      p.drawText(QRect(r.left() + 16, r.center().y() + r.height() / 8, r.width() - 32, r.height() / 4),
                 Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, sub);
    }

    QColor accent_{"#7c3aed"};
    QColor muted_{"#80868f"};
    bool activeLeft_ = true;
    QTimer pulse_;
    double phase_ = 0.0;
  };

}  // namespace stencil::gui

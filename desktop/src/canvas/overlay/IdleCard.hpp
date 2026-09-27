#pragma once
#include <QRectF>

class QPainter;
class QString;
class QVariantAnimation;
class QWidget;

// The empty canvas's "＋ Blank image" card (browser .idle-create-btn): its paint, and its motion —
// the hover blend, the glyph settle, the glass sweep and the arrival when a picture leaves, each a
// QVariantAnimation on the host, which repaints on every tick and wears the hover's cursor.
namespace stencil::gui {

  struct Palette;

  class IdleCard {
   public:
    explicit IdleCard(QWidget* host) : host(host) {}

    // The page fill and the card on it; `dark` and `accentKey` are the host's look.
    void paint(QPainter& p, const Palette& pal, bool dark, const QString& accentKey);
    void setHover(bool on);
    // The card grows in from the centre the editor just emptied (browser @keyframes idleCardArrive).
    void startArrival();
    // Widget space, as last painted: the RESTING rect, never the lifted one.
    const QRectF& cardRect() const { return cardBox; }

    bool hidden = false;   // off while the clear's dust falls (.canvas-clearing)

   private:
    QWidget* host;
    QRectF cardBox;
    bool hover = false;
    double hoverT = 0.0;   // hover blend 0..1, over 0.2s as the browser transitions it
    QVariantAnimation* hoverAnim = nullptr;
    double enterT = 1.0;   // the arrival when a picture leaves; 1 = resting
    QVariantAnimation* enterAnim = nullptr;
    double shimmerT = -1.0;
    QVariantAnimation* shimmerAnim = nullptr;
    // Stroked by hand in IdleCard.cpp (the app-wide watcher knows only QAbstractButtons, and
    // Qt6::Svg would follow it into every headless target): iconMotion.json `image`. -1 = rest.
    double glyphMs = -1.0;
    QVariantAnimation* glyphAnim = nullptr;
  };

}  // namespace stencil::gui

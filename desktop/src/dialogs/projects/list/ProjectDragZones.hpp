#pragma once
// Three-zone drop overlay over the whole MAIN WINDOW behind the Projects dialog (browser projects-modal
// drag zones, which cover the page): open here / new window / remove. Visual + a cursor poll only; the
// dialog decides the ACTION from the release position via zoneAt(). Header-only and Q_OBJECT-free.
#include "iconSet.hpp"
#include "theme.hpp"

#include <QColor>
#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QLatin1String>
#include <QList>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QString>
#include <QSvgRenderer>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace stencil::gui {

  class ProjectDragZones : public QWidget {
   public:
    enum class Zone { NONE, HERE, NEW_WINDOW, REMOVE };

    explicit ProjectDragZones(QWidget* parent) : QWidget(parent) {
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      hide();
      poll.setInterval(16);
      // A cursor poll: the modal dialog's blocking drag loop delivers no drag-move events here.
      // The phase step makes the browser's 0.9s dropZonePulse cycle (2π · 16ms / 900ms).
      QObject::connect(&poll, &QTimer::timeout, [this] {
        hover = zoneAt(QCursor::pos());
        phase += 0.1117;
        if (phase > 6.2831853) phase -= 6.2831853;
        update();
      });
    }

    // `dialog` is never a zone. Fills the parent window, and stays under a dialog shown inside it
    // (the popover form), as the browser's zones lie under the card.
    void begin(QWidget* dialog) {
      dialogFrame = dialog->isWindow() ? dialog->frameGeometry()
                                       : QRect(dialog->mapToGlobal(QPoint(0, 0)), dialog->size());
      if (parentWidget()) setGeometry(parentWidget()->rect());
      hover = Zone::NONE;
      pal = themePalette(palette().color(QPalette::Window).lightness() < 128);
      raise();
      for (QWidget* w = dialog; w && !w->isWindow(); w = w->parentWidget())
        if (w->parentWidget() == parentWidget()) { stackUnder(w); break; }
      show();
      poll.start();
    }
    void end() { poll.stop(); hide(); }

    Zone zoneAt(const QPoint& global) const {
      if (dialogFrame.contains(global)) return Zone::NONE;
      if (!parentWidget()) return Zone::NONE;
      const QPoint p = parentWidget()->mapFromGlobal(global);
      if (!rect().contains(p)) return Zone::NONE;
      if (p.y() > height() * SPLIT) return Zone::REMOVE;
      return p.x() < width() / 2 ? Zone::HERE : Zone::NEW_WINDOW;
    }

    // Browser twin: css/components/projects/projects.css .project-dropzones / .pdz*. The top 70% splits
    // Open here | the other window 16px apart; Remove fills the bottom 30% less a 10px gap.
    static QRect zoneRect(Zone z, const QSize& area) {
      const int w = area.width(), h = area.height();
      const int half = w / 2 - GUTTER / 2, top = qRound(h * SPLIT), low = qRound(h * (1 - SPLIT)) - REMOVE_GAP;
      if (z == Zone::HERE) return QRect(0, 0, half, top);
      if (z == Zone::NEW_WINDOW) return QRect(w - half, 0, half, top);
      return z == Zone::REMOVE ? QRect(0, h - low, w, low) : QRect();
    }

    // Each zone's own colour, --pdz: slate, blue, the theme's danger.
    static QColor pdzOf(Zone z, const Palette& pal) {
      const std::optional<QRgb> pdz = specOf(z).pdz;
      return pdz ? QColor::fromRgb(*pdz) : pal.danger;
    }

    // Fill pdz 28% (48% when over) into --bg-container at 0.9 (0.92) alpha; dashes pdz at 0.72;
    // the icon and label pdz 65% into white.
    struct Look { QColor fill, dash, ink; };
    static Look lookOf(const QColor& pdz, const QColor& bgContainer, bool over) {
      Look l{mixSrgb(bgContainer, pdz, over ? 0.48 : 0.28), pdz, mixSrgb(QColor(Qt::white), pdz, 0.65)};
      l.fill.setAlphaF(over ? 0.92 : 0.9);
      l.dash.setAlphaF(0.72);
      return l;
    }

    // The icon-over-label box: 30px in from the outer side and 26 down at the top corners, centred
    // 20 up from the bottom under Remove — always clear of the centred dialog.
    QRect labelRect(Zone z) const {
      const QRect inner = zoneRect(z, size()).adjusted(BORDER, BORDER, -BORDER, -BORDER);
      const QFontMetrics fm(labelFont());
      const QSize box(std::max(fm.horizontalAdvance(specOf(z).title), ICON), ICON + GAP + fm.height());
      const int x = z == Zone::HERE ? inner.left() + 30
                  : z == Zone::NEW_WINDOW ? inner.right() + 1 - 30 - box.width()
                                          : inner.center().x() - box.width() / 2;
      const int y = z == Zone::REMOVE ? inner.bottom() + 1 - 20 - box.height() : inner.top() + 26;
      return QRect(QPoint(x, y), box);
    }

    // A zone glyph `px` wide about `centre`, as the browser's icon(name, {size: 22}) under its pulse:
    // the canon's vector drawn at the final size, so the 2/24 stroke never thins in a pixmap rescale.
    static void drawGlyph(QPainter& g, const QString& name, const QColor& ink, const QPointF& centre, double px) {
      // Three glyphs × a theme's inks.
      static std::map<std::pair<QString, QRgb>, std::unique_ptr<QSvgRenderer>> drawn;
      auto& art = drawn[{name, ink.rgba()}];
      if (!art) art = std::make_unique<QSvgRenderer>(iconSvgDocument(iconMarkup(name), ink).toUtf8());
      g.save();
      g.setRenderHint(QPainter::Antialiasing, true);
      art->render(&g, QRectF(centre.x() - px / 2, centre.y() - px / 2, px, px));
      g.restore();
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter g(this);
      g.setRenderHint(QPainter::Antialiasing, true);
      // No scrim of its own: the gaps show the modal backdrop's, as the browser's show the overlay's.
      for (const ZoneSpec& spec : SPECS) drawZone(g, spec);
    }

   private:
    static constexpr int BORDER = 5, RADIUS = 12, ICON = 22, GAP = 6, GUTTER = 16, REMOVE_GAP = 10;
    static constexpr double SPLIT = 0.7;   // the open zones' share of the height, the rest Remove's

    struct ZoneSpec { Zone zone; QLatin1String glyph, title; std::optional<QRgb> pdz; };   // no pdz = danger
    static constexpr ZoneSpec SPECS[] = {
      {Zone::HERE, QLatin1String("folder"), QLatin1String("Open here"), 0xff64748b},
      {Zone::NEW_WINDOW, QLatin1String("external"), QLatin1String("Open in a new window"), 0xff2563eb},
      {Zone::REMOVE, QLatin1String("trash"), QLatin1String("Remove"), std::nullopt},
    };

    static const ZoneSpec& specOf(Zone z) {
      const auto* spec =
          std::find_if(std::begin(SPECS), std::end(SPECS), [z](const ZoneSpec& s) { return s.zone == z; });
      return spec != std::end(SPECS) ? *spec : SPECS[std::size(SPECS) - 1];
    }

    QFont labelFont() const {
      QFont f = font();
      f.setPixelSize(14);
      f.setWeight(QFont::DemiBold);
      return f;
    }

    void drawZone(QPainter& g, const ZoneSpec& spec) {
      static const QList<qreal> DASH{2.0, 1.0};   // Chromium's dashed border: 2w on, 1w off
      const Zone z = spec.zone;
      const QRect r = zoneRect(z, size());
      const Look look = lookOf(pdzOf(z, pal), pal.bgContainer, hover == z);
      g.setPen(Qt::NoPen);
      g.setBrush(look.fill);
      g.drawRoundedRect(QRectF(r), RADIUS, RADIUS);
      QPen pen(look.dash, BORDER, Qt::CustomDashLine, Qt::FlatCap);
      pen.setDashPattern(DASH);
      g.setPen(pen);
      g.setBrush(Qt::NoBrush);
      const double mid = BORDER / 2.0;
      g.drawRoundedRect(QRectF(r).adjusted(mid, mid, -mid, -mid), RADIUS - mid, RADIUS - mid);

      const QRect box = labelRect(z);
      // The browser's 0.8–1.25 pulse about the glyph's centre.
      const QPointF centre(box.left() + box.width() / 2.0, box.top() + ICON / 2.0);
      drawGlyph(g, spec.glyph, look.ink, centre, ICON * (1.025 - 0.225 * std::cos(phase)));
      g.setPen(look.ink);
      g.setFont(labelFont());
      g.drawText(box.adjusted(0, ICON + GAP, 0, 0), Qt::AlignHCenter | Qt::AlignTop, spec.title);
    }

    QTimer poll;
    Zone hover = Zone::NONE;
    double phase = 0.0;
    QRect dialogFrame;
    Palette pal;
  };

}  // namespace stencil::gui

#include "colorDragParts.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTableWidget>
#include <memory>
#include <optional>

namespace stencil::support {

  using namespace colorDragParts;

  QColor colorToApply(const ColorSwatch& source, const ColorSwatch& target) {
    const QColor s = source.read ? source.read() : QColor();
    if (!s.isValid() || s.alpha() == 0) return {};
    const QColor t = target.read ? target.read() : QColor();
    QColor c = s;
    c.setAlpha(source.alpha && target.alpha ? s.alpha() : (target.alpha && t.isValid() ? t.alpha() : 255));
    return t.isValid() && t.alpha() > 0 && t.rgba() == c.rgba() ? QColor() : c;
  }

  namespace colorDragParts {
    QHash<QWidget*, Cells>& tables() {
      static QHash<QWidget*, Cells> registry;
      return registry;
    }

    std::optional<Spot> cellSpot(const Cells& cells, int row, int column) {
      if (!cells.table || row < 0 || column < 0) return std::nullopt;
      ColorSwatch s = cells.at(row, column);
      if (!s.read) return std::nullopt;
      return Spot{cells.table->cellWidget(row, column), std::move(s)};
    }
  }  // namespace colorDragParts

  namespace {
    constexpr int CHIP_PX = 22;
    constexpr int CHIP_OFFSET_PX = 12;   // the browser's CHIP_OFFSET_PX: the swatch under the pointer stays seen
    constexpr int CHECKER_PX = 4;

    // The colour riding beside the pointer, over a checkerboard so a translucent one reads as one.
    class ColorChip : public QWidget {
     public:
      ColorChip(QWidget* window, const QColor& color) : QWidget(window), color(color) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        resize(CHIP_PX, CHIP_PX);
        show();
      }
      void follow(const QPoint& global) {
        move(parentWidget()->mapFromGlobal(global) + QPoint(CHIP_OFFSET_PX, CHIP_OFFSET_PX));
        raise();
      }

     protected:
      void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        QPainterPath shape;
        shape.addRoundedRect(box, 6, 6);
        p.setClipPath(shape);
        for (int y = 0; y < height(); y += CHECKER_PX)
          for (int x = 0; x < width(); x += CHECKER_PX)
            p.fillRect(x, y, CHECKER_PX, CHECKER_PX, (x + y) / CHECKER_PX % 2 ? QColor(200, 200, 200) : QColor(Qt::white));
        p.fillRect(rect(), color);
        p.setClipping(false);
        p.setPen(QPen(palette().color(QPalette::Base), 2));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
      }

     private:
      QColor color;
    };

    QHash<QWidget*, ColorSwatch>& chips() {
      static QHash<QWidget*, ColorSwatch> registry;
      return registry;
    }

    std::optional<Spot> spotAt(QWidget* w, const QPoint& global) {
      for (; w; w = w->parentWidget()) {
        if (auto c = chips().constFind(w); c != chips().constEnd()) return Spot{w, *c};
        if (auto t = tables().constFind(w); t != tables().constEnd() && t->table) {
          const QModelIndex idx = t->table->indexAt(w->mapFromGlobal(global));
          return cellSpot(*t, idx.row(), idx.column());
        }
        if (w->isWindow()) break;
      }
      return std::nullopt;
    }

    // Every swatch that could take a drop now; under a modal, only the ones in the source's window.
    QList<Spot> liveSpots(const QWidget* from) {
      const bool modal = QApplication::activeModalWidget() != nullptr;
      const auto reachable = [&](const QWidget* w) { return !modal || w->window() == from->window(); };
      QList<Spot> out;
      for (auto c = chips().constBegin(); c != chips().constEnd(); ++c)
        if (Spot s{c.key(), *c}; reachable(c.key()) && s.live()) out << s;
      for (auto t = tables().constBegin(); t != tables().constEnd(); ++t) {
        if (!t->table || !reachable(t->table)) continue;
        for (int r = 0; r < t->table->rowCount(); ++r)
          for (int col = 0; col < t->table->columnCount(); ++col)
            if (const std::optional<Spot> s = cellSpot(*t, r, col);
                s && s->live() && !s->glow->visibleRegion().isEmpty())
              out << *s;
      }
      return out;
    }

    struct DragState {
      ColorSwatch source;
      QPointer<ColorChip> chip;
      QList<Spot> lit;
      QPointer<QWidget> over;
    };
  }  // namespace

  IconDragHooks colorDragParts::colorHooks(std::function<std::optional<Spot>()> lift, QWidget* owner) {
    auto st = std::make_shared<DragState>();
    const auto end = [st] {
      delete st->chip.data();
      for (const Spot& t : st->lit) markDropTarget(t.glow, false);
      st->lit.clear();
      st->over = nullptr;
    };
    // Only a swatch lit at the start takes the drop: shown, enabled and not the source.
    const auto litAt = [st](const QPoint& global) -> std::optional<Spot> {
      const std::optional<Spot> hit = spotAt(QApplication::widgetAt(global), global);
      for (const Spot& t : st->lit)
        if (hit && t.glow == hit->glow) return t;
      return std::nullopt;
    };
    IconDragHooks h;
    h.ghost = false;
    h.start = [st, lift, owner](const QPoint&, const QPoint& global) {
      const std::optional<Spot> s = lift();
      if (!s || !s->live() || s->swatch.read().alpha() == 0) return false;
      st->source = s->swatch;
      st->chip = new ColorChip(owner->window(), s->swatch.read());
      st->chip->follow(global);
      for (const Spot& t : liveSpots(owner))
        if (t.glow != s->glow) {
          st->lit << t;
          markDropTarget(t.glow, true);
        }
      return true;
    };
    h.move = [st, litAt](const IconDragPoint& p) {
      if (st->chip) st->chip->follow(p.global);
      const std::optional<Spot> t = litAt(p.global);
      QWidget* now = t ? t->glow.data() : nullptr;
      if (now == st->over) return;
      if (st->over) markDropTarget(st->over, true, false);
      st->over = now;
      if (now) markDropTarget(now, true, true);
    };
    h.drop = [st, end, litAt](const IconDragPoint& p) {
      const std::optional<Spot> t = litAt(p.global);
      const ColorSwatch from = st->source;
      end();
      const QColor c = t ? colorToApply(from, t->swatch) : QColor();
      if (c.isValid()) t->swatch.apply(c);
    };
    h.cancel = end;
    return h;
  }

  void installColorDrag(QAbstractButton* chip, ColorSwatch swatch) {
    if (!chip) return;
    chips().insert(chip, swatch);
    QObject::connect(chip, &QObject::destroyed, [chip] { chips().remove(chip); });
    installIconDrag(chip, colorHooks([chip] { return std::optional<Spot>(Spot{chip, chips().value(chip)}); }, chip));
  }

}  // namespace stencil::support

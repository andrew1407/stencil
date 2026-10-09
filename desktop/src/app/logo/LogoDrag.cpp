#include "LogoDrag.hpp"
#include "LogoStage.hpp"
#include "dblReset.hpp"
#include "iconDrag.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QToolButton>
#include <memory>

namespace stencil::gui {

  namespace {
    // The mark centred on a button-sized face, so the ghost holds it where the button showed it.
    QPixmap ghostFace(const QToolButton* logo, const QPixmap& mark) {
      if (mark.isNull()) return mark;
      const qreal dpr = mark.devicePixelRatio();
      QPixmap face(logo->size() * dpr);
      face.setDevicePixelRatio(dpr);
      face.fill(Qt::transparent);
      QPainter p(&face);
      const QSizeF art = mark.deviceIndependentSize();
      p.drawPixmap(QPointF((logo->width() - art.width()) / 2, (logo->height() - art.height()) / 2), mark);
      return face;
    }

    // What the pointer aims at; a line before a control, since the selected-line bar holds controls.
    struct Aim {
      enum Kind { NONE, CLEAN, LINE, CONTROL } kind = NONE;
      QPointer<QWidget> glow;
      int idx = -1;
      bool operator==(const Aim& o) const { return kind == o.kind && glow == o.glow && idx == o.idx; }
    };
  }  // namespace

  void installLogoDrag(QToolButton* logo, const LogoStage* stage, LogoDragHooks hooks) {
    auto h = std::make_shared<LogoDragHooks>(std::move(hooks));
    auto over = std::make_shared<Aim>();
    const auto area = [h]() -> QWidget* { return h->canvas ? h->canvas() : nullptr; };
    const auto picture = [h] { return h->hasImage && h->hasImage(); };
    const auto aimAt = [h, area, picture](const support::IconDragPoint& p) {
      if (!p.target) return Aim{};
      if (const LogoLineAim line = h->lineAt ? h->lineAt(p.target, p.global) : LogoLineAim{}; line.idx >= 0)
        return Aim{Aim::LINE, line.glow, line.idx};
      if (QWidget* control = support::dropResetTarget(p.target)) return Aim{Aim::CONTROL, control};
      QWidget* canvas = area();
      if (canvas && picture() && (p.target == canvas || canvas->isAncestorOf(p.target)))
        return Aim{Aim::CLEAN, canvas};
      return Aim{};
    };
    const auto show = [h, area](const Aim& a, bool on) {
      if (a.kind == Aim::CLEAN && h->preview) h->preview(on);
      if (a.kind == Aim::LINE && h->lineHover) h->lineHover(on ? a.idx : -1);
      if (a.glow && a.glow == area()) support::markDropTarget(a.glow, true, on);
      else if (a.glow) support::markDropTarget(a.glow, on, on);
    };
    const auto aim = [over, show](const Aim& next) {
      if (*over == next) return;
      show(*over, false);
      *over = next;
      show(*over, true);
    };
    const auto end = [aim, area] {
      aim(Aim{});
      support::markDropTarget(area(), false);
    };
    support::IconDragHooks drag;
    drag.start = [h, area, picture, held = QPointer<const LogoStage>(stage)](const QPoint&, const QPoint&) {
      if (QGuiApplication::keyboardModifiers() != Qt::NoModifier || (h->free && !h->free())) return false;
      if (held && (held->isOpen() || held->pressOpenedShow())) return false;
      support::markDropTarget(area(), picture());
      return true;
    };
    drag.move = [aim, aimAt](const support::IconDragPoint& p) { aim(aimAt(p)); };
    drag.drop = [h, end, aimAt](const support::IconDragPoint& p) {
      const Aim a = aimAt(p);
      end();
      if (a.kind == Aim::CLEAN && h->commit) h->commit();
      if (a.kind == Aim::LINE && h->styleLine) h->styleLine(a.idx);
      if (a.kind == Aim::CONTROL) support::resetToDefault(a.glow);
    };
    drag.cancel = end;
    drag.face = [h, logo] { return ghostFace(logo, h->mark ? h->mark() : QPixmap()); };
    support::installIconDrag(logo, std::move(drag));
  }

}  // namespace stencil::gui

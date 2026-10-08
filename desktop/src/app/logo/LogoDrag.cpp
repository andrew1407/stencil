#include "LogoDrag.hpp"
#include "LogoStage.hpp"
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
  }  // namespace

  void installLogoDrag(QToolButton* logo, const LogoStage* stage, LogoDragHooks hooks) {
    auto h = std::make_shared<LogoDragHooks>(std::move(hooks));
    auto over = std::make_shared<bool>(false);
    const auto area = [h]() -> QWidget* { return h->canvas ? h->canvas() : nullptr; };
    const auto picture = [h] { return h->hasImage && h->hasImage(); };
    const auto onCanvas = [area, picture](QWidget* at) {
      QWidget* canvas = area();
      return canvas && at && picture() && (at == canvas || canvas->isAncestorOf(at));
    };
    const auto show = [h, over, area](bool on) {
      if (*over == on) return;
      *over = on;
      support::markDropTarget(area(), true, on);
      if (h->preview) h->preview(on);
    };
    const auto end = [show, area] {
      show(false);
      support::markDropTarget(area(), false);
    };
    support::IconDragHooks drag;
    drag.start = [h, area, picture, held = QPointer<const LogoStage>(stage)](const QPoint&, const QPoint&) {
      if (QGuiApplication::keyboardModifiers() != Qt::NoModifier || (h->free && !h->free())) return false;
      if (held && (held->isOpen() || held->pressOpenedShow())) return false;
      support::markDropTarget(area(), picture());
      return true;
    };
    drag.move = [show, onCanvas](const support::IconDragPoint& p) { show(onCanvas(p.target)); };
    drag.drop = [h, end, onCanvas](const support::IconDragPoint& p) {
      const bool on = onCanvas(p.target);
      end();
      if (on && h->commit) h->commit();
    };
    drag.cancel = end;
    drag.face = [h, logo] { return ghostFace(logo, h->mark ? h->mark() : QPixmap()); };
    support::installIconDrag(logo, std::move(drag));
  }

}  // namespace stencil::gui

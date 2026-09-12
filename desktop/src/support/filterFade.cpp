#include "filterFade.hpp"

namespace stencil::gui {


  // Presence → opacity. Pure.
  double filterOpacity(double presence) {
    const double t = (std::clamp(presence, 0.0, 1.0) - kFilterFadeLead) / (1.0 - kFilterFadeLead);
    return std::clamp(t, 0.0, 1.0);
  }


  // Presence → the slot's share of its natural height, smoothstepped so the collapse
  // eases in and out instead of snapping. Pure.
  double filterHeightFraction(double presence) {
    const double t = std::clamp(presence, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
  }


  // …and in pixels. A settled row is left at exactly its natural height (no rounding
  // drift), which is what keeps a rapid sequence of filter changes exactly reversible.
  int filterHeight(int fullHeight, double presence) {
    if (fullHeight <= 0) return fullHeight;
    if (presence >= 1.0) return fullHeight;
    return std::max(0, int(std::lround(fullHeight * filterHeightFraction(presence))));
  }


  // A row's presence, for painted rows (an item-view delegate has no widget to read).
  // Rows the filter has never touched are fully present.
  double filterPresenceOf(const QModelIndex& idx) {
    const QVariant v = idx.data(kFilterPresenceRole);
    return v.isValid() ? std::clamp(v.toDouble(), 0.0, 1.0) : 1.0;
  }


  // How much of the row's INK to paint: its presence, held back further while its dust is
  // still gathering. A row with no dust in flight reads 1 and this is the fade alone.
  double filterInk(const QModelIndex& idx) {
    const QVariant v = idx.data(kFilterDustRole);
    const double veil = v.isValid() ? std::clamp(v.toDouble(), 0.0, 1.0) : 1.0;
    return filterOpacity(filterPresenceOf(idx)) * veil;
  }


  // Whether a row belongs to the CURRENT filtered set — the TARGET of any transition in
  // flight, so a row still fading out already counts as gone. Use this instead of
  // isHidden() wherever "the filtered view" is the pool (select-all, counts).
  bool filteredIn(const QListWidgetItem* it) {
    if (!it) return false;
    const QVariant t = it->data(kFilterTargetRole);
    return t.isValid() ? t.toDouble() > 0.5 : !it->isHidden();
  }


  // The same transition for a plain laid-out widget a filter shows/hides. A grid row has
  // no slot to collapse, so this is the fade alone; the widget is hidden (and handed back
  // without an effect) once it has gone. Rapid changes retarget ONE animation per widget,
  // never stack them, so the last filter always wins.
  void fadeFiltered(QWidget* w, bool show) {
    if (!w) return;
    auto* anim = w->findChild<QVariantAnimation*>(QString::fromLatin1(kFilterFadeAnimName),
                                                  Qt::FindDirectChildrenOnly);
    if (anim) anim->stop();   // mid-flight stop() never emits finished()
    if (support::motionReduced()) {   // straight to the end state
      w->setGraphicsEffect(nullptr);
      w->setVisible(show);
      return;
    }
    auto* fx = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    const double from = fx ? fx->opacity() : (w->isVisible() ? 1.0 : 0.0);
    const double to = show ? 1.0 : 0.0;
    if (from == to) {
      w->setVisible(show);
      if (!show) w->setGraphicsEffect(nullptr);
      return;
    }
    if (show) w->setVisible(true);   // it has to be up to be seen arriving
    if (!fx) {
      fx = new QGraphicsOpacityEffect(w);
      w->setGraphicsEffect(fx);
    }
    fx->setOpacity(from);
    if (!anim) {
      anim = new QVariantAnimation(w);
      anim->setObjectName(QString::fromLatin1(kFilterFadeAnimName));
      QObject::connect(anim, &QVariantAnimation::valueChanged, w, [w](const QVariant& v) {
        if (auto* e = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect()))
          e->setOpacity(v.toDouble());
      });
      QObject::connect(anim, &QVariantAnimation::finished, w, [w, anim] {
        if (anim->endValue().toDouble() <= 0.0) w->setVisible(false);
        w->setGraphicsEffect(nullptr);   // hand the widget back untouched
      });
    }
    anim->setDuration(std::max(1, int(kFilterFadeMs * std::abs(to - from))));
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->start();
  }
}  // namespace stencil::gui

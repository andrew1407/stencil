#include "filterFade.hpp"

namespace stencil::gui {


  // Presence → opacity. Pure.
  double filterOpacity(double presence) {
    const double t = (std::clamp(presence, 0.0, 1.0) - FILTER_FADE_LEAD) / (1.0 - FILTER_FADE_LEAD);
    return std::clamp(t, 0.0, 1.0);
  }


  // Smoothstepped so the collapse eases instead of snapping. Pure.
  double filterHeightFraction(double presence) {
    const double t = std::clamp(presence, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
  }


  // A settled row lands at exactly its natural height, so a rapid sequence of filter
  // changes is exactly reversible.
  int filterHeight(int fullHeight, double presence) {
    if (fullHeight <= 0) return fullHeight;
    if (presence >= 1.0) return fullHeight;
    return std::max(0, int(std::lround(fullHeight * filterHeightFraction(presence))));
  }


  // Rows the filter has never touched are fully present.
  double filterPresenceOf(const QModelIndex& idx) {
    const QVariant v = idx.data(FILTER_PRESENCE_ROLE);
    return v.isValid() ? std::clamp(v.toDouble(), 0.0, 1.0) : 1.0;
  }


  // A row with no dust in flight reads 1 and this is the fade alone.
  double filterInk(const QModelIndex& idx) {
    const QVariant v = idx.data(FILTER_DUST_ROLE);
    const double veil = v.isValid() ? std::clamp(v.toDouble(), 0.0, 1.0) : 1.0;
    return filterOpacity(filterPresenceOf(idx)) * veil;
  }


  // The TARGET of any transition in flight: a row fading out already counts as gone.
  bool filteredIn(const QListWidgetItem* it) {
    if (!it) return false;
    const QVariant t = it->data(FILTER_TARGET_ROLE);
    return t.isValid() ? t.toDouble() > 0.5 : !it->isHidden();
  }


  // A grid row has no slot to collapse: the fade alone, hidden once it has gone. Rapid
  // changes retarget ONE animation per widget, so the last filter always wins.
  void fadeFiltered(QWidget* w, bool show) {
    if (!w) return;
    auto* anim = w->findChild<QVariantAnimation*>(QString::fromLatin1(FILTER_FADE_ANIM_NAME),
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
      anim->setObjectName(QString::fromLatin1(FILTER_FADE_ANIM_NAME));
      QObject::connect(anim, &QVariantAnimation::valueChanged, w, [w](const QVariant& v) {
        if (auto* e = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect()))
          e->setOpacity(v.toDouble());
      });
      QObject::connect(anim, &QVariantAnimation::finished, w, [w, anim] {
        if (anim->endValue().toDouble() <= 0.0) w->setVisible(false);
        w->setGraphicsEffect(nullptr);   // hand the widget back untouched
      });
    }
    anim->setDuration(std::max(1, int(FILTER_FADE_MS * std::abs(to - from))));
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->start();
  }
}  // namespace stencil::gui

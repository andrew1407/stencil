#include "ghostLabel.hpp"

#include <QVariantAnimation>

#include <algorithm>
#include <utility>

namespace stencil::support::genie {

  QLabel* makeGhostLabel(QWidget* host, const QPixmap& shot) {
    auto* ghost = new QLabel(host);
    ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghost->setScaledContents(true);
    ghost->setPixmap(shot);
    return ghost;
  }

  void flyGhostLabel(QLabel* ghost, int ms, QEasingCurve::Type curve, std::function<void(QLabel&, double)> place,
                     std::function<void()> done) {
    place(*ghost, 0.0);
    ghost->show();
    ghost->raise();
    auto* anim = new QVariantAnimation(ghost);
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(curve);
    QObject::connect(anim, &QVariantAnimation::valueChanged, ghost,
                     [ghost, place = std::move(place)](const QVariant& v) { place(*ghost, v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, ghost, [ghost, done = std::move(done)] {
      if (done) done();
      ghost->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  QRect tailBox(const QRect& box, double s, bool isRight) {
    const QSize sz(std::max(1, static_cast<int>(box.width() * s)), std::max(1, static_cast<int>(box.height() * s)));
    return QRect(QPoint(isRight ? box.right() - sz.width() + 1 : box.left(), box.bottom() - sz.height() + 1), sz);
  }

}  // namespace stencil::support::genie

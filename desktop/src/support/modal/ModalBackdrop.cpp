// ModalBackdrop's bodies: the photograph, its blur and scrim, the fades and the retake.
// The class, its constants and what each member means are in ModalBackdrop.hpp.
#include "ModalBackdrop.hpp"

#include <QApplication>
#include <QEasingCurve>
#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace stencil::support {

  QList<QWidget*> ModalBackdrop::aboveChildren(QWidget* host) {
    QList<QWidget*> out;
    if (!host) return out;
    for (QWidget* w : host->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
      if (w->isVisible() && w->property(ABOVE_PROPERTY).toBool()) out.append(w);
    return out;
  }

  QPixmap ModalBackdrop::blurred(const QPixmap& shot, int radiusPx) {
    if (shot.isNull() || radiusPx < 1) return shot;
    const qreal dpr = shot.devicePixelRatio() > 0 ? shot.devicePixelRatio() : 1.0;
    const int div = radiusPx * 2;   // two 1/(2r) passes read softer than one of 1/r
    const QSize small(qMax(2, int(shot.width() / dpr) / div),
                      qMax(2, int(shot.height() / dpr) / div));
    QPixmap out = shot.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .scaled(shot.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    out.setDevicePixelRatio(dpr);
    return out;
  }

  ModalBackdrop* ModalBackdrop::behind(QDialog* dlg, QWidget* host) {
    if (!modalBackdrop() || !dlg || !host) return nullptr;
    if (host->width() < 8 || host->height() < 8) return nullptr;
    // One per dialog; and a backdrop still up (one fading out behind a window just closed) stays
    // out of the photograph, or the darkness doubles (two 50% scrims read as 75%).
    QList<QWidget*> up = host->findChildren<QWidget*>(QStringLiteral("modalBackdrop"), Qt::FindDirectChildrenOnly);
    for (QWidget* w : up)
      if (static_cast<ModalBackdrop*>(w)->owner == dlg) return static_cast<ModalBackdrop*>(w);
    const QPixmap raw = photograph(host, up);
    if (raw.isNull()) return nullptr;
    auto* bd = new ModalBackdrop(host);
    bd->owner = dlg;
    bd->setPhoto(raw);
    bd->show();
    bd->raise();
    for (QWidget* w : aboveChildren(host)) w->raise();
    // Over a backdrop already up it starts dark: fading from its undimmed photo flashed grey (user report).
    bool dimmed = false;
    for (QWidget* w : up) dimmed = dimmed || static_cast<ModalBackdrop*>(w)->level >= 0.99;
    bd->fadeTo(1.0, dimmed ? 0 : FADE_IN_MS, /*thenDrop=*/false);
    // Fades with the dialog's flight back — accepted or stale, as a bare scrim over the LIVE window, so the arrival
    // plays in view (user report); over one still dark it drops at once, its fade bared an undimmed photo (user report).
    QObject::connect(dlg, &QDialog::finished, bd, [bd](int result) {
      if (result == QDialog::Accepted || bd->stale) {
        bd->live = true;
        bd->setOpaque(false);
      }
      bd->fadeTo(0.0, bd->darkBelow() ? 0 : FADE_OUT_MS, true);
    });
    return bd;
  }

  void ModalBackdrop::retakeOn(QWidget* host, const QList<QWidget*>& skip) {
    for (ModalBackdrop* bd : on(host)) {
      const QPixmap raw = photograph(host, skip + QList<QWidget*>{bd});
      if (!raw.isNull()) bd->crossTo(raw);
    }
  }

  void ModalBackdrop::followFor(QWidget* host, int ms) {
    for (ModalBackdrop* bd : on(host)) bd->stale = true;
    QTimer::singleShot(motionReduced() ? 0 : motionMs(ms), host, [h = QPointer<QWidget>(host)] { if (h) retakeOn(h); });
  }

  QPixmap ModalBackdrop::cleanShot(QWidget* w) {
    const QList<ModalBackdrop*> over = on(w);
    return w ? photograph(w, QList<QWidget*>(over.cbegin(), over.cend())) : QPixmap();
  }

  void ModalBackdrop::behindAll(QDialog* dlg) {
    if (!dlg) return;
    for (QWidget* w : QApplication::topLevelWidgets()) {
      const Qt::WindowType type = w->windowType();
      if (!w->isVisible() || w == dlg->window() || type == Qt::Popup ||
          type == Qt::ToolTip || type == Qt::SplashScreen)
        continue;
      behind(dlg, w);
    }
  }

  void ModalBackdrop::clearAll() {
    for (QWidget* top : QApplication::topLevelWidgets()) for (ModalBackdrop* bd : on(top)) bd->fadeTo(0.0, FADE_OUT_MS, true);
  }

  void ModalBackdrop::fadeTo(double to, int ms, bool thenDrop) {
    if (fade) fade->stop();
    if (motionReduced() || ms <= 0) {
      level = to;
      update();
      if (thenDrop) { hide(); deleteLater(); }
      return;
    }
    auto* anim = new QVariantAnimation(this);
    fade = anim;
    anim->setDuration(motionMs(ms));
    anim->setStartValue(level);
    anim->setEndValue(to);
    anim->setEasingCurve(QEasingCurve::InOutQuad);   // CSS `ease`
    QObject::connect(anim, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) { level = v.toDouble(); update(); });
    if (thenDrop)
      QObject::connect(anim, &QAbstractAnimation::finished, this, [this] { hide(); deleteLater(); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void ModalBackdrop::paintEvent(QPaintEvent*) {
    if (shooting) return;   // being photographed: what lies under it is the picture
    QPainter p(this);
    if (live) { p.fillRect(rect(), QColor(0, 0, 0, int(255 * SCRIM_ALPHA * level))); return; }
    // Opaque over the window it photographed, so no frame of its fade (or of a flight over it)
    // repaints that window's widgets beneath it, which dropped frames mid-flight.
    if (opaque && level < 1.0 && !raw.isNull()) p.drawPixmap(rect(), raw);
    // One layer fading as a whole, as the browser's overlay does: a fade frame is one blend, at rest a blit.
    p.setOpacity(level);
    if (mix < 1.0 && !was.isNull()) {
      p.drawPixmap(rect(), was);
      p.setOpacity(level * mix);
    }
    p.drawPixmap(rect(), dimmed);
  }

  void ModalBackdrop::setPhoto(const QPixmap& window) {
    raw = window;
    dimmed = blurred(window, BLUR_PX);
    QPainter p(&dimmed);
    p.fillRect(QRectF(QPointF(0, 0), dimmed.deviceIndependentSize()), QColor(0, 0, 0, static_cast<int>(255 * SCRIM_ALPHA)));
  }

  QList<ModalBackdrop*> ModalBackdrop::on(QWidget* host) {
    QList<ModalBackdrop*> out;
    for (QWidget* w : host ? host->findChildren<QWidget*>(QStringLiteral("modalBackdrop"), Qt::FindDirectChildrenOnly) : QList<QWidget*>()) out.append(static_cast<ModalBackdrop*>(w));
    return out;
  }

  QPixmap ModalBackdrop::photograph(QWidget* host, const QList<QWidget*>& skip) {
    QList<QWidget*> hidden;
    QList<ModalBackdrop*> quiet;
    for (QWidget* w : skip + aboveChildren(host)) {
      if (!w || !w->isVisible()) continue;
      if (w->objectName() == QLatin1String("modalBackdrop")) {
        quiet.append(static_cast<ModalBackdrop*>(w));
      } else {
        w->setVisible(false);
        hidden.append(w);
      }
    }
    for (ModalBackdrop* bd : quiet) bd->setShooting(true);
    const QPixmap shot = host->grab();
    for (QWidget* w : hidden) w->setVisible(true);
    for (ModalBackdrop* bd : quiet) bd->setShooting(false);
    return shot;
  }

  void ModalBackdrop::crossTo(const QPixmap& window) {
    was = dimmed;
    setPhoto(window);
    mix = 1.0;
    if (motionReduced() || was.isNull()) { update(); return; }
    if (cross) cross->stop();
    cross = new QVariantAnimation(this);   // a retake mid-cross starts over from the picture now shown
    cross->setDuration(motionMs(CROSS_MS));
    QObject::connect(cross, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { mix = v.toDouble(); update(); });
    cross->setStartValue(0.0);
    cross->setEndValue(1.0);
    cross->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void ModalBackdrop::setOpaque(bool on) {
    opaque = on;
    setAttribute(Qt::WA_OpaquePaintEvent, on);
    update();
  }

  bool ModalBackdrop::darkBelow() const {
    const QList<ModalBackdrop*> all = on(parentWidget());
    return std::any_of(all.begin(), all.end(), [this](const ModalBackdrop* o) {
      return o != this && o->owner && o->owner != owner && o->owner->isVisible() && o->level >= 0.99;
    });
  }

  ModalBackdrop::ModalBackdrop(QWidget* host) : QWidget(host) {
    setObjectName(QStringLiteral("modalBackdrop"));
    setAttribute(Qt::WA_TransparentForMouseEvents);   // the dialog is modal; this only paints
    setGeometry(host->rect());
    setAttribute(Qt::WA_OpaquePaintEvent, true);
  }

}  // namespace stencil::support

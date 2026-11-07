#include "chatDockShared.hpp"
#include "../../support/theme/faceSwap.hpp"

#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/motion/scrollReveal.hpp"        // ENTERING_PROPERTY — the entrance claims the effect
#include "../../support/motion/ShimmerOverlay.hpp"  // the shared hover sweep on every ghost button
#include "../../support/motionPrefs.hpp"
#include "../../support/rowWork.hpp"

#include <QApplication>
#include <QEasingCurve>
#include <cmath>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QImageReader>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui::chatdock {

  // Sink + dissolve `w` in place, then delete it. The snapshot was taken already, so the two play
  // together as they do in the browser: the bubble melts into its own dust rather than vanishing.
  void fadeOutAndDelete(QWidget* w) {
    if (!w) return;
    // Nothing may move: the card goes NOW, unfaded (browser leaveThenRemove).
    if (support::motionReduced()) { w->hide(); w->deleteLater(); return; }
    // CLAIM the card's effect for the length of the fade: ScrollReveal installs its own DissolveEffect
    // near a viewport edge and setGraphicsEffect DELETES the one already there. animateCardIn too.
    w->setProperty(ScrollReveal::ENTERING_PROPERTY, true);
    // A card removed mid-entrance still has its appear animation writing to the effect it installed,
    // and setGraphicsEffect() DELETES that one - so stop the card's animations first and REUSE it.
    for (QVariantAnimation* a : w->findChildren<QVariantAnimation*>()) a->stop();
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (!fx) {
      fx = veilBehindDust(w);   // the widget owns the effect
      fx->setOpacity(1.0);
    }
    const double from = fx->opacity();
    fx->setOpacity(from);
    auto* anim = new QVariantAnimation(w);
    anim->setDuration(support::motionMs(CHAT_LEAVE_MS));
    anim->setStartValue(from);   // continue from wherever the entrance got to
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // QPointer, not a raw capture: belt and braces for the same hazard — if anything
    // ever replaces the effect mid-flight again, the write is skipped, not fatal.
    QPointer<QGraphicsOpacityEffect> fxp(fx);
    QObject::connect(anim, &QVariantAnimation::valueChanged, w,
                     [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, w, [w] {
      w->hide();
      w->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  QIcon liftedIcon(const QIcon& icon, int px, double gain) {
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QImage img = icon.pixmap(QSize(px, px), dpr).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto up = [gain](int c, int a) { return std::min(a, static_cast<int>(std::lround(c * gain))); };   // premultiplied: never past alpha
    for (int y = 0; y < img.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
      for (int x = 0; x < img.width(); ++x) {
        const int a = qAlpha(row[x]);
        row[x] = qRgba(up(qRed(row[x]), a), up(qGreen(row[x]), a), up(qBlue(row[x]), a), a);
      }
    }
    QPixmap out = QPixmap::fromImage(img);
    out.setDevicePixelRatio(dpr);
    return QIcon(out);
  }

  QToolButton* makeGhostButton(QWidget* parent, const QString& tooltip) {
    auto* b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setIconSize(QSize(HEADER_ICON, HEADER_ICON));
    b->setFixedSize(BUTTON_EDGE, BUTTON_EDGE);
    b->setToolTip(tooltip);
    b->setCursor(Qt::PointingHandCursor);
    // The browser shimmers every <button>, chat controls included (layout.css ui-shimmer); this is the
    // one factory behind ALL of them, so the sweep lands on each exactly once. Mouse-through.
    installHoverShimmer(b);
    return b;
  }

  void repolish(QWidget* w) {
    gui::repolish(w);
  }

  void readImageFilesThen(QObject* ctx, const QStringList& paths,
                          std::function<void(const NamedImages&)> done) {
    support::runOnPool<NamedImages>(ctx, [paths] {
      NamedImages read;
      for (const QString& p : paths) {
        QImageReader reader(p);
        reader.setAutoTransform(true);
        read.append({reader.read(), QFileInfo(p).fileName()});
      }
      return read;
    }, std::move(done));
  }

  bool queueAttachments(QList<QImage>& images, QStringList& names, const NamedImages& read,
                        QStringList* failed) {
    bool overCap = false;
    for (const auto& [img, name] : read) {
      if (img.isNull()) {
        if (failed) *failed << name;
        continue;
      }
      if (images.size() >= MAX_ATTACHMENTS) { overCap = true; continue; }   // §7: three per message
      images.append(img);
      names.append(name);
    }
    return overCap;
  }

}  // namespace stencil::gui::chatdock

namespace stencil::gui::chatdock {

  QPixmap coverThumb(const QImage& image, int edge, int radius, const QColor& border, qreal dpr) {
    QPixmap pm(QSize(edge, edge) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    if (image.isNull() || edge <= 0) return pm;
    const QImage scaled = image.scaled(QSize(edge, edge) * dpr, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);
    const QRect crop((scaled.width() - edge * dpr) / 2, (scaled.height() - edge * dpr) / 2,
                     edge * dpr, edge * dpr);
    QPainter p(&pm);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, edge, edge), radius, radius);
    p.setClipPath(clip);
    p.drawImage(QRectF(0, 0, edge, edge), scaled, crop);
    p.setClipping(false);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(0.5, 0.5, edge - 1, edge - 1), radius, radius);
    return pm;
  }

}  // namespace stencil::gui::chatdock

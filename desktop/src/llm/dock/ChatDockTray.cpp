// The attachment tray: chips, their scatter-on-remove, the tray's visibility.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/motionPrefs.hpp"
#include "theme.hpp"
#include "chatWidgets.hpp"

#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <QFontMetrics>
#include <functional>

namespace stencil::gui {

  using namespace chatdock;
  void ChatDock::refreshAttachmentTray() {
    auto* row = qobject_cast<QHBoxLayout*>(cmp.attachTray->layout());
    if (!row) return;
    // Drop the previous chips (the trailing stretch is re-added last).
    while (QLayoutItem* item = row->takeAt(0)) {
      if (QWidget* w = item->widget()) w->deleteLater();
      delete item;
    }
    // One chip per queued attachment. `index` is captured by value, and every removal rebuilds the
    // whole row, so the handlers can never act on a stale position.
    const auto addChip = [this, row](const QPixmap& thumb, const QString& label,
                                     const QString& tip, std::function<void()> remove,
                                     const QImage& full = QImage()) {
      auto* chip = new QFrame(cmp.attachTray);
      chip->setObjectName("chatAttachChip");
      auto* lay = new QHBoxLayout(chip);
      lay->setContentsMargins(4, 2, 4, 2);
      lay->setSpacing(6);
      if (!thumb.isNull()) {
        auto* pic = new QLabel(chip);
        pic->setPixmap(thumb);
        pic->setFixedSize(thumb.size());
        // 28px can't tell two screenshots apart — hovering shows the full picture.
        if (!full.isNull()) new HoverPreview(pic, full, tip);
        lay->addWidget(pic);
      }
      auto* text = makePlainLabel(label, chip);   // a dropped filename is untrusted
      // A long filename must not widen the DOCK (the tray fed minimumSizeHint, which
      // both auto-grew the panel and blocked shrinking it) — elide, tooltip has it all.
      const QFontMetrics chipFm(text->font());
      text->setText(chipFm.elidedText(label, Qt::ElideMiddle, CHIP_NAME_MAX_PX));
      // The full name/size live on the LABEL. The thumbnail deliberately carries no tooltip: it opens
      // the hover preview, and a Qt tooltip over that covered the picture it described.
      text->setToolTip(tip);
      lay->addWidget(text);
      auto* rm = new QToolButton(chip);
      rm->setObjectName("chatAttachRemove");
      rm->setText(QStringLiteral("×"));
      rm->setAccessibleName(QStringLiteral("Remove attachment"));  // no tooltip — the × says it
      rm->setCursor(Qt::PointingHandCursor);
      // The chip scatters AND fades, and the tray only rebuilds once it has gone - rebuilding at once
      // snapped the composer mid-flight. The neighbours' slide is eased in (user feedback).
      connect(rm, &QToolButton::clicked, this, [this, remove, chip] {
        if (chip->property("chatChipLeaving").toBool()) return;   // one click is enough
        chip->setProperty("chatChipLeaving", true);
        // Nothing may move: the slot closes NOW, unheld (browser chipLeave) — the
        // rebuild remove() runs is what drops this chip.
        if (support::motionReduced()) { remove(); return; }
        DisintegrateOverlay::over(chip, window(), DisintegrateOverlay::Sweep::FALL,
                                  CHAT_SCATTER_COLS, CHAT_SCATTER_ROWS, 0,
                                  chip->palette().color(QPalette::WindowText));
        // Fade the chip itself out (the scatter replaces it visually) WITHOUT hiding
        // or deleting it — an invisible chip still holds its slot for the hold+squeeze.
        for (QVariantAnimation* a : chip->findChildren<QVariantAnimation*>()) a->stop();
        auto* fx = qobject_cast<QGraphicsOpacityEffect*>(chip->graphicsEffect());
        if (!fx) { fx = new QGraphicsOpacityEffect(chip); chip->setGraphicsEffect(fx); }
        auto* fade = new QVariantAnimation(chip);
        fade->setDuration(CHAT_LEAVE_MS);
        fade->setStartValue(fx->opacity());
        fade->setEndValue(0.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        QPointer<QGraphicsOpacityEffect> fxp(fx);
        connect(fade, &QVariantAnimation::valueChanged, chip,
                [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
        // Hold the slot while the scatter reads, then collapse the width gently so the
        // surviving chips glide over (browser .chat-attach-chip.leaving parity).
        QTimer::singleShot(CHAT_CHIP_HOLD_MS, chip, [this, chip, remove] {
          auto* squeeze = new QVariantAnimation(chip);
          squeeze->setDuration(CHAT_LEAVE_MS);
          squeeze->setStartValue(chip->width());
          squeeze->setEndValue(0);
          squeeze->setEasingCurve(QEasingCurve::InOutCubic);
          connect(squeeze, &QVariantAnimation::valueChanged, chip,
                  [chip](const QVariant& v) { chip->setMaximumWidth(v.toInt()); });
          squeeze->start(QAbstractAnimation::DeleteWhenStopped);
          QTimer::singleShot(CHAT_LEAVE_MS, this, [chip, remove] {
            chip->deleteLater();
            remove();
          });
        });
      });
      lay->addWidget(rm);
      row->addWidget(chip);
    };

    for (int i = 0; i < cmp.images.size(); ++i) {
      const QImage& img = cmp.images.at(i);
      const QPixmap thumb = QPixmap::fromImage(
          img.scaled(QSize(28, 28), Qt::KeepAspectRatio, Qt::SmoothTransformation));
      const QString name = i < cmp.imageNames.size() ? cmp.imageNames.at(i) : QString();
      const QString dims = QStringLiteral("%1×%2").arg(img.width()).arg(img.height());
      addChip(thumb, name.isEmpty() ? dims : name,
              name.isEmpty() ? QStringLiteral("Queued image (%1)").arg(dims)
                             : QStringLiteral("%1 (%2)").arg(name, dims),
              [this, i] {
                if (i < cmp.images.size()) {
                  cmp.images.removeAt(i);
                  if (i < cmp.imageNames.size()) cmp.imageNames.removeAt(i);
                }
                refreshAttachmentTray();
              },
              img);
    }
    if (!cmp.videoPath.isEmpty()) {
      addChip(QPixmap(), QFileInfo(cmp.videoPath).fileName(),
              QStringLiteral("Queued video — frames are sent, never the video (%1)").arg(cmp.videoPath),
              [this] {
                cmp.videoPath.clear();
                refreshAttachmentTray();
                emit videoDetached();
              });
    }
    row->addStretch(1);
    cmp.attachTray->setVisible(!cmp.images.isEmpty() || !cmp.videoPath.isEmpty());
  }
}  // namespace stencil::gui

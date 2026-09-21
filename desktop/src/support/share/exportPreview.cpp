#include "exportPreview.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>

#include "DisintegrateOverlay.hpp"
#include "modalReveal.hpp"
#include "tipContent.hpp"

namespace stencil::support {

  namespace {
    constexpr int PREVIEW_MAX = 220;  // px, longest edge of the rendered thumbnail
    // The preview is sand too (browser js/ui/exportPreview.js), on the shared tip clock.

    QWidget* tipWindow() {
      static QWidget* w = nullptr;
      if (!w) {
        // WindowTransparentForInput, not just the widget attribute: a native top-level
        // stays input-OPAQUE to the OS without it and eats the hover under the pointer.
        w = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint |
                                     Qt::WindowTransparentForInput |
                                     Qt::WindowDoesNotAcceptFocus);
        w->setAttribute(Qt::WA_ShowWithoutActivating);
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
        w->setObjectName("exportPreviewTip");
        auto* lay = new QVBoxLayout(w);
        lay->setContentsMargins(4, 4, 4, 4);
        auto* label = new QLabel(w);
        label->setObjectName("exportPreviewLabel");
        lay->addWidget(label);
      }
      return w;
    }

    QPointer<QWidget> lastOwner;
    QRect lastOwnerRect;
    bool tipClosing = false;

    QVariantAnimation* tipFade() {
      static QVariantAnimation* a = nullptr;
      if (!a) {
        a = new QVariantAnimation(tipWindow());
        QObject::connect(a, &QVariantAnimation::valueChanged, tipWindow(),
                         [](const QVariant& v) { tipWindow()->setWindowOpacity(v.toDouble()); });
        QObject::connect(a, &QVariantAnimation::finished, tipWindow(), [] {
          if (!tipClosing) return;
          tipClosing = false;
          tipWindow()->hide();
        });
      }
      return a;
    }

    // The menu chain's real window hosts the flight: a layer inside the popup is
    // clipped to the menu's rect, and a top-level one steals the menu's platform grab.
    QWidget* dustHost(QWidget* owner) {
      QWidget* w = owner;
      while (auto* m = qobject_cast<QMenu*>(w)) w = m->parentWidget();
      if (w) return w->window();
      return owner ? owner->window() : nullptr;
    }

    // `overrideGlobal`: an Alt press/release flies from the cursor, not the row.
    bool dust(QWidget* owner, const QRect& ownerRect, bool gather,
              const QPoint& overrideGlobal = QPoint()) {
      if (!owner || !owner->isVisible() || !ownerRect.isValid()) return false;
      const QPoint origin =
          overrideGlobal.isNull() ? owner->mapToGlobal(ownerRect.center()) : overrideGlobal;
      // NOT escapeHost: a second top-level window steals the open menu's platform grab
      // and closes it the instant Alt is pressed.
      return gui::flyTipDust(tipWindow(), dustHost(owner), origin, gather,
                             gather ? gui::TIP_DUST_IN_MS : gui::TIP_DUST_OUT_MS,
                             /*escapeHost=*/false, /*paintNow=*/!gather)
             != nullptr;
    }
  }  // namespace

  void showExportPreview(const QImage& image, QWidget* owner, const QRect& ownerRect,
                         const QPoint& dustFromGlobal) {
    if (image.isNull()) {
      hideExportPreview();
      return;
    }
    QWidget* w = tipWindow();
    // Read at every show so a theme swap between two hovers repaints it.
    const gui::Palette pal = gui::currentPalette();
    w->setStyleSheet(QStringLiteral("#exportPreviewTip { background:%1; border:1px solid %2; "
                                    "border-radius:8px; }")
                         .arg(pal.bgContainer.name(), pal.borderMain.name()));
    // A re-show that never left its row (per-move QMenu::hovered re-fires) glides.
    const bool appearing = !w->isVisible() || tipClosing;
    const bool sameRow = !appearing && owner == lastOwner && ownerRect == lastOwnerRect;
    if (!sameRow) {
      auto* label = w->findChild<QLabel*>("exportPreviewLabel");
      label->setPixmap(QPixmap::fromImage(image).scaled(
          PREVIEW_MAX, PREVIEW_MAX, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      w->adjustSize();
    }

    const QPoint cur = QCursor::pos();
    QPoint pos = cur + QPoint(18, 18);
    if (QScreen* scr = QGuiApplication::screenAt(cur)) {
      const QRect avail = scr->availableGeometry();
      if (pos.x() + w->width() > avail.right()) pos.setX(cur.x() - w->width() - 18);
      if (pos.y() + w->height() > avail.bottom()) pos.setY(cur.y() - w->height() - 18);
      pos.setX(std::max(avail.left(), pos.x()));
      pos.setY(std::max(avail.top(), pos.y()));
    }
    lastOwner = owner;
    lastOwnerRect = ownerRect;
    w->move(pos);
    if (!appearing) return;
    tipClosing = false;   // BEFORE stop(): stop() emits finished, which would hide()
    auto* fade = tipFade();
    fade->stop();
    fade->setKeyValues({});
    if (support::motionReduced()) {
      w->setWindowOpacity(1.0);
      w->show();
      return;
    }
    w->setWindowOpacity(0.0);
    w->show();
    if (dust(owner, ownerRect, /*gather=*/true, dustFromGlobal)) {
      gui::holdFadeKeys(fade, gui::TIP_DUST_IN_MS);
      fade->start();
    } else {
      w->setWindowOpacity(1.0);
    }
  }

  void hideExportPreview(const QPoint& dustToGlobal) {
    QWidget* w = tipWindow();
    if (tipClosing) return;
    if (!w->isVisible()) { lastOwner.clear(); lastOwnerRect = QRect(); return; }
    // Dusted while the row is still known.
    const bool dusted =
        !support::motionReduced() && dust(lastOwner.data(), lastOwnerRect, /*gather=*/false, dustToGlobal);
    lastOwner.clear();
    lastOwnerRect = QRect();
    auto* fade = tipFade();
    fade->stop();
    fade->setKeyValues({});
    if (!dusted) {
      w->hide();
      return;
    }
    tipClosing = true;
    fade->setDuration(gui::DUST_HAND_OVER_MS);
    fade->setStartValue(w->windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  QWidget* exportPreviewOwner() {
    return tipWindow()->isVisible() ? lastOwner.data() : nullptr;
  }

  QRect exportPreviewOwnerRect() { return lastOwnerRect; }

}  // namespace stencil::support

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

#include "disintegrateOverlay.hpp"
#include "modalReveal.hpp"   // support::motionReduced()
#include "tipContent.hpp"    // gui::currentPalette() — the theme the tip is painted in

namespace stencil::support {

  namespace {
    constexpr int kPreviewMax = 220;  // px, longest edge of the rendered thumbnail
    // ── The preview is sand too (browser js/ui/exportPreview.js) ────────────────
    // It forms from motes streaming out of the row it previews and comes apart into
    // motes pouring back into it, on the shared tip clock (disintegrateOverlay.hpp):
    // short, so a flight is over before an Alt-hover sweep reaches the next row.

    // Lazily built, reused across shows — same idea as canvasTooltip.cpp's floating
    // readout: a tooltip-flagged frameless window that never steals focus/clicks.
    QWidget* tipWindow() {
      static QWidget* w = nullptr;
      if (!w) {
        // WindowTransparentForInput, not just the widget attribute: a native top-level
        // stays input-OPAQUE to the OS without it, and a tip clamped under the pointer
        // then eats the hover (the projects-dialog preview's churn bug, same fix).
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
    bool tipClosing = false;   // the fade below is running towards hide()

    // The tip's windowOpacity ramp — in behind the gather, out behind the leave.
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

    // The layer the flight is drawn in. The OWNER (a QMenu popup) is its own tiny
    // top-level window — a child overlay there was clipped to the menu's rect, and
    // the journey to the tip (floating beside the cursor, mostly OFF the menu) was
    // simply cropped away (user report: the animation was nearly invisible). The
    // chain's real window, under every popup, hosts it instead; a top-level layer
    // stays off the table — it steals the menu's platform grab (see the escapeHost
    // note in dust()).
    QWidget* dustHost(QWidget* owner) {
      QWidget* w = owner;
      while (auto* m = qobject_cast<QMenu*>(w)) w = m->parentWidget();
      if (w) return w->window();
      return owner ? owner->window() : nullptr;
    }

    // Fly the preview's own motes out of — or back into — the row it belongs to,
    // or into `overrideGlobal` when the trigger was a KEY, not the pointer (Alt
    // pressed/released = the flight belongs to the cursor, not the row).
    bool dust(QWidget* owner, const QRect& ownerRect, bool gather,
              const QPoint& overrideGlobal = QPoint()) {
      if (!owner || !owner->isVisible() || !ownerRect.isValid()) return false;
      const QPoint origin =
          overrideGlobal.isNull() ? owner->mapToGlobal(ownerRect.center()) : overrideGlobal;
      // NOT escapeHost — `host` is the OPEN, Alt-hovered menu itself, still holding the
      // platform grab (this fires mid-hover): an escape layer's second top-level window
      // steals that grab and closes the menu the instant Alt is pressed.
      return gui::flyTipDust(tipWindow(), dustHost(owner), origin, gather,
                             gather ? gui::kTipDustInMs : gui::kTipDustOutMs,
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
    // The theme's own card (--bg-container over --border-main), read at every show so a
    // theme swap between two hovers repaints it.
    const gui::Palette pal = gui::currentPalette();
    w->setStyleSheet(QStringLiteral("#exportPreviewTip { background:%1; border:1px solid %2; "
                                    "border-radius:8px; }")
                         .arg(pal.bgContainer.name(), pal.borderMain.name()));
    // An APPEARANCE is a FRESH show only: hover-out hides the preview (AltPreviewFilter's
    // MouseMove check), so landing on another row arrives here hidden and gathers anew;
    // a re-show that never left its row (per-move QMenu::hovered re-fires) glides.
    const bool appearing = !w->isVisible() || tipClosing;
    // Same-row re-fire: the pixmap is already up — skip the convert/scale/relayout.
    const bool sameRow = !appearing && owner == lastOwner && ownerRect == lastOwnerRect;
    if (!sameRow) {
      auto* label = w->findChild<QLabel*>("exportPreviewLabel");
      label->setPixmap(QPixmap::fromImage(image).scaled(
          kPreviewMax, kPreviewMax, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    if (support::motionReduced()) {  // the end state, at once
      w->setWindowOpacity(1.0);
      w->show();
      return;
    }
    // The tip waits behind its own gathering motes and fades up as the last of them
    // land — shown at once, it covered the very flight that forms it (user report).
    w->setWindowOpacity(0.0);
    w->show();
    if (dust(owner, ownerRect, /*gather=*/true, dustFromGlobal)) {
      gui::holdFadeKeys(fade, gui::kTipDustInMs);
      fade->start();
    } else {
      w->setWindowOpacity(1.0);   // no flight to wait behind
    }
  }

  void hideExportPreview(const QPoint& dustToGlobal) {
    QWidget* w = tipWindow();
    if (tipClosing) return;
    if (!w->isVisible()) { lastOwner.clear(); lastOwnerRect = QRect(); return; }
    // Photographed and dusted while the row is still known — the cloud is what the
    // preview leaves behind; the tip itself fades out BEHIND the leaving motes over
    // one beat instead of blinking off under them.
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
    fade->setDuration(gui::kDustHandOverMs);
    fade->setStartValue(w->windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  QWidget* exportPreviewOwner() {
    return tipWindow()->isVisible() ? lastOwner.data() : nullptr;
  }

  QRect exportPreviewOwnerRect() { return lastOwnerRect; }

}  // namespace stencil::support

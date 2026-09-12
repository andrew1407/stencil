#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "stayOpenMenu.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "chatMenuPanel.hpp"
#include "fetchGuard.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "cropDialog.hpp"
#include "guiHelpers.hpp"
#include "menuHotkeys.hpp"
#include "menuReveal.hpp"
#include "menuShimmer.hpp"
#include "modalReveal.hpp"
#include "searchCombo.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/appTooltip.hpp"
#include "../support/controlSwap.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"
#include "../support/hoverSlide.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

// The logo's accent-preset popover and its hover preview.

namespace stencil::gui {

  namespace {
    // A FIRST-CLASS popover dialog (not a QMenu), so the popover system's Alt-peek/glide/linger rules apply. Presets from theme.cpp accentPresets.
    // The Settings dropdown's swatch recipe; the CURRENT accent's ✓ is baked into its chip in the chip's OWN ink (browser accentPicker.js).
    QIcon accentSwatchIcon(const QColor& c, bool current) {
      QPixmap pm(16, 16);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(QColor(0, 0, 0, 70), 1));
      p.setBrush(c);
      p.drawRoundedRect(1, 1, 13, 13, 3, 3);
      if (current) {
        p.setBrush(Qt::NoBrush);
        const QPointF pts[3] = {{4.4, 8.3}, {6.9, 10.7}, {11.4, 5.3}};
        p.setPen(QPen(onAccentInk(c), 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(pts, 3);
      }
      p.end();
      return QIcon(pm);
    }
  }  // namespace

  // Move the ✓ in an OPEN popover to the accent settings now hold (browser toolbar.js onAccentMoved twin). Touches only the row icons.
  void MainWindow::remarkAccentPopover() {
    // The popover being exec'd may sit under the overlay layer, so it is not looked up by parent.
    QDialog* pop = pop_.active.data();
    if (!pop || pop->objectName() != QLatin1String("accentPopover")) return;
    for (QPushButton* r : pop->findChildren<QPushButton*>()) {
      const QString rowKey = r->property("accentKey").toString();
      if (rowKey.isEmpty()) continue;
      const bool now = rowKey == settings_.accentColor;   // a custom #… accent marks nothing
      if (r->property("currentAccent").toBool() == now) continue;
      r->setIcon(accentSwatchIcon(QColor(accentPrimary(rowKey)), now));
      r->setProperty("currentAccent", now);
    }
  }

  void MainWindow::previewAccent(const QString& key) {
    if (!accentPreviewActive_) { accentPreviewSaved_ = settings_.accentColor; accentPreviewActive_ = true; }
    if (key == settings_.accentColor) return;
    settings_.accentColor = key;
    applyTheme();   // floods the palette out of the logo, exactly as a real change does
  }

  void MainWindow::endAccentPreview() {
    if (!accentPreviewActive_) return;
    accentPreviewActive_ = false;
    if (settings_.accentColor == accentPreviewSaved_) return;
    settings_.accentColor = accentPreviewSaved_;
    applyTheme();   // …and floods back to the committed accent on leave
  }

  namespace {
    // A hop between rows is not a leave; the preview waits for the pointer to settle so the accent flood fires once. Browser twin: accentPicker.js.
    class AccentHoverFilter : public QObject {
     public:
      AccentHoverFilter(QObject* parent, QWidget* popover,
                        std::function<void(const QString&)> onEnter, std::function<void()> onLeave)
          : QObject(parent), popover_(popover), enter_(std::move(onEnter)), leave_(std::move(onLeave)) {
        timer_.setSingleShot(true);
        timer_.setInterval(280);   // rested-intent delay — matches the JS surfaces (PREVIEW_HOVER_MS)
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { if (!pending_.isEmpty()) enter_(pending_); });
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Enter) {
          auto* w = qobject_cast<QWidget*>(o);
          const QString key = w ? w->property("accentKey").toString() : QString();
          if (!key.isEmpty()) { pending_ = key; timer_.start(); }   // fires once the pointer rests
        } else if (e->type() == QEvent::Leave && popover_) {
          const QPoint p = popover_->mapFromGlobal(QCursor::pos());
          if (!popover_->rect().contains(p)) { timer_.stop(); pending_.clear(); leave_(); }  // truly left
        }
        return QObject::eventFilter(o, e);
      }
      QWidget* popover_;
      std::function<void(const QString&)> enter_;
      std::function<void()> leave_;
      QTimer timer_;
      QString pending_;
    };
  }  // namespace

  void MainWindow::openAccentPicker() {
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("accentPopover"));
    // Resting on a row previews it app-wide (no persist); leaving without a pick reverts.
    auto* hover = new AccentHoverFilter(&dlg, &dlg,
        [this](const QString& key) { previewAccent(key); }, [this] { endAccentPreview(); });
    dlg.installEventFilter(hover);
    auto* col = new QVBoxLayout(&dlg);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(1);
    // Rows are built once and RE-MARKED in place whenever the accent moves: picking must not rebuild or move the popover.
    for (const AccentPreset& a : accentPresets()) {
      const bool current = a.key == settings_.accentColor;   // a custom #… accent marks nothing
      auto* row = new QPushButton(&dlg);
      row->setObjectName(QStringLiteral("accentRow-") + a.key);
      row->setFlat(true);
      row->setCursor(Qt::PointingHandCursor);
      row->setIconSize(QSize(16, 16));
      row->setIcon(accentSwatchIcon(QColor(a.hex), current));
      row->setText(a.label);
      row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      row->setProperty("accentKey", a.key);         // observable by the GUI test
      row->setProperty("currentAccent", current);
      row->installEventFilter(hover);   // Enter previews this preset (see AccentHoverFilter)
      // The browser row's `transform: translateX(2px)`; after the preview filter so the preview sees Enter first.
      installHoverSlide(row);
      connect(row, &QPushButton::clicked, &dlg, [this, key = a.key] {
        accentPreviewActive_ = false;   // a pick commits; the close below must not revert it
        auto next = settings_;
        next.accentColor = key;
        applySettings(next, true);   // the click-cycle's apply + persist path; re-marks the ✓ via applyTheme
        // A pick CLOSES the popover (hovering already previews live). Browser twin: the logo menu.
        dismissPopover();
      });
      col->addWidget(row);
    }
    execMaybePopover(dlg);
    endAccentPreview();   // closed while a row was still hovered → back to the committed accent
  }

}  // namespace stencil::gui

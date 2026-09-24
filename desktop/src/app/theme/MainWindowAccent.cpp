#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "ChatMenuPanel.hpp"
#include "fetchGuard.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "CropDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "MenuHotkeys.hpp"
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "modalReveal.hpp"
#include "SearchCombo.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "../../support/skinPrefs.hpp"
#include <QStyle>
#include "../../support/tip/AppTooltip.hpp"
#include "../../support/control/swap/controlSwap.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/motion/HoverSlide.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

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
    // The Settings dropdown's swatch recipe; the CURRENT accent's ✓ is baked into its chip in the chip's OWN ink (browser accent/picker.js).
    QIcon accentSwatchIcon(const QColor& c, bool current) {
      const bool skin = support::isWebcore();
      const qreal dpr = skin ? 2.0 : 1.0;   // the skin's pixel ✓ needs the room
      QPixmap pm(QSize(16, 16) * dpr);
      pm.setDevicePixelRatio(dpr);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, !skin);
      p.setPen(QPen(skin ? QColor(Qt::black) : QColor(0, 0, 0, 70), 1));
      p.setBrush(c);
      if (skin) p.drawRect(1, 1, 13, 13); else p.drawRoundedRect(1, 1, 13, 13, 3, 3);
      if (current && skin) {
        // The skin's own green ✓ art, never the chip's ink (browser: icon('check') under webcore).
        const QPixmap tick = themedIcon("check", QColor(), 11, dpr).pixmap(QSize(11, 11), dpr);
        p.drawPixmap(QRectF(2.5, 2.5, 11, 11), tick, QRectF(tick.rect()));
      } else if (current) {
        p.setBrush(Qt::NoBrush);
        const QPointF pts[3] = {{4.4, 8.3}, {6.9, 10.7}, {11.4, 5.3}};
        p.setPen(QPen(onAccentInk(c), 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(pts, 3);
      }
      p.end();
      return QIcon(pm);
    }

    // One lit row (browser .logo-accent-menu): the pointer's, else the current preset's.
    void relightAccentRows(QWidget* pop, const QWidget* hovered) {
      for (QPushButton* r : pop->findChildren<QPushButton*>()) {
        const bool lit = hovered ? r == hovered : r->property("currentAccent").toBool();
        if (r->property("lit").toBool() == lit) continue;
        r->setProperty("lit", lit);
        r->style()->unpolish(r);
        r->style()->polish(r);
      }
    }
  }  // namespace

  // Move the ✓ in an OPEN popover to the accent settings now hold (browser toolbar.js onAccentMoved twin). Touches only the row icons.
  void MainWindow::remarkAccentPopover() {
    // The popover being exec'd may sit under the overlay layer, so it is not looked up by parent.
    QDialog* pop = this->pop.active.data();
    if (!pop || pop->objectName() != QLatin1String("accentPopover")) return;
    for (QPushButton* r : pop->findChildren<QPushButton*>()) {
      const QString rowKey = r->property("accentKey").toString();
      if (rowKey.isEmpty()) continue;
      const bool now = rowKey == settings.accentColor;   // a custom #… accent marks nothing
      if (r->property("currentAccent").toBool() == now) continue;
      r->setIcon(accentSwatchIcon(QColor(accentPrimary(rowKey)), now));
      r->setProperty("currentAccent", now);
    }
    QPushButton* hovered = nullptr;
    for (QPushButton* r : pop->findChildren<QPushButton*>()) if (r->underMouse()) hovered = r;
    relightAccentRows(pop, hovered);
  }

  void MainWindow::previewAccent(const QString& key) {
    if (!accentPreviewActive) { accentPreviewSaved = settings.accentColor; accentPreviewActive = true; }
    if (key == settings.accentColor) return;
    settings.accentColor = key;
    applyTheme();   // floods the palette out of the logo, exactly as a real change does
  }

  void MainWindow::endAccentPreview() {
    if (!accentPreviewActive) return;
    accentPreviewActive = false;
    if (settings.accentColor == accentPreviewSaved) return;
    settings.accentColor = accentPreviewSaved;
    applyTheme();   // …and floods back to the committed accent on leave
  }

  namespace {
    // A hop between rows is not a leave; the preview waits for the pointer to settle so the accent flood fires once. Browser twin: accent/picker.js.
    class AccentHoverFilter : public QObject {
     public:
      AccentHoverFilter(QObject* parent, QWidget* popover,
                        std::function<void(const QString&)> onEnter, std::function<void()> onLeave)
          : QObject(parent), popover(popover), enter(std::move(onEnter)), leave(std::move(onLeave)) {
        timer.setSingleShot(true);
        timer.setInterval(280);   // rested-intent delay — matches the JS surfaces (PREVIEW_HOVER_MS)
        QObject::connect(&timer, &QTimer::timeout, this, [this] { if (!pending.isEmpty()) enter(pending); });
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Enter) {
          auto* w = qobject_cast<QWidget*>(o);
          const QString key = w ? w->property("accentKey").toString() : QString();
          if (!key.isEmpty()) { pending = key; timer.start(); relightAccentRows(popover, w); }   // previews once rested
        } else if (e->type() == QEvent::Leave && popover) {
          const QPoint p = popover->mapFromGlobal(QCursor::pos());
          if (!popover->rect().contains(p)) {   // truly left
            timer.stop();
            pending.clear();
            leave();
            relightAccentRows(popover, nullptr);
          }
        }
        return QObject::eventFilter(o, e);
      }
      QWidget* popover;
      std::function<void(const QString&)> enter;
      std::function<void()> leave;
      QTimer timer;
      QString pending;
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
    const int inset = support::isWebcore() ? 3 : 8;   // browser webcore .logo-accent-menu padding
    col->setContentsMargins(inset, inset, inset, inset);
    col->setSpacing(1);
    // Rows are built once and RE-MARKED in place whenever the accent moves: picking must not rebuild or move the popover.
    for (const AccentPreset& a : accentPresets()) {
      const bool current = a.key == settings.accentColor;   // a custom #… accent marks nothing
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
        accentPreviewActive = false;   // a pick commits; the close below must not revert it
        auto next = settings;
        next.accentColor = key;
        applySettings(next, true);   // the click-cycle's apply + persist path; re-marks the ✓ via applyTheme
        // A pick CLOSES the popover (hovering already previews live). Browser twin: the logo menu.
        dismissPopover();
      });
      col->addWidget(row);
    }
    relightAccentRows(&dlg, nullptr);
    execMaybePopover(dlg);
    endAccentPreview();   // closed while a row was still hovered → back to the committed accent
  }

}  // namespace stencil::gui

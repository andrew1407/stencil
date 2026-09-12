// buildMainToolbar()'s first row: the always-visible header — the app logo with its accent
// click/dblclick/Alt-peek gestures, the "Controls" collapse pill, the project-name group, and
// the Image Size label that is placed in its own bar below. Its addToolBarBreak() ends the row.
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "iconSet.hpp"
#include "logoHoverFx.hpp"
#include "controlsPill.hpp"
#include "searchCombo.hpp"
#include "openImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"
#include "../support/wrapRow.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace stencil::gui {

  // The "Controls" collapse pill, the logo and its accent gestures, and the project-name
  // group. This row stays put while the tool row below slides open and closed.
  void MainWindow::buildHeaderRow() {
    // Header row (always visible): the "Controls" collapse pill + the project-name group.
    // This row stays put while the tool rows below (Main / Page&Formula / Style) slide open/closed,
    // exactly like the browser's header that keeps the "⌃ Controls" pill + title when the body hides.
    headerToolbar_ = addToolBar("Header");
    headerToolbar_->setObjectName("headerToolbar");  // named for QMainWindow::saveState
    headerToolbar_->setMovable(false);
    // App logo (the mini S mark, mirrors the browser's top-left logo). Clicking it cycles the theme
    // accent to the next preset — the same affordance as the browser's clickable logo.
    logoBtn_ = new QToolButton(this);
    logoBtn_->setCursor(Qt::PointingHandCursor);
    logoBtn_->setIconSize(QSize(kHeaderLogo, kHeaderLogo));
    // Size the BUTTON to the mark: a QToolBar otherwise lays an added widget out at the
    // toolbar's default icon metric, so the badge stayed ~40px however large its icon
    // (the logo would not grow). The margin leaves the hover fx room.
    logoBtn_->setFixedSize(kHeaderLogo + 6, kHeaderLogo + 6);
    logoBtn_->setIcon(QIcon(makeLogoPixmap(kHeaderLogo)));
    // LogoHoverFx paints the resting mark and blanks this icon — QToolButton draws it at
    // half size on Retina. The fx owns BOTH states.
    logoBtn_->setToolTip(QString());   // no tooltip on the logo
    // No hover highlight — flat, transparent, borderless (just the logo art).
    logoBtn_->setStyleSheet("QToolButton{border:none;background:transparent;padding:2px;}");
    // Single click cycles the accent, but DEFER it briefly so a double-click can pre-empt it and open
    // the custom-colour picker instead (mirrors the browser logo's click-vs-dblclick behaviour).
    logoClickTimer_ = new QTimer(this);
    logoClickTimer_->setSingleShot(true);
    connect(logoClickTimer_, &QTimer::timeout, this, [this] {
      const auto& presets = accentPresets();
      if (presets.empty()) return;
      int idx = -1;
      for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].key == settings_.accentColor) { idx = static_cast<int>(i); break; }
      auto next = settings_;
      // Browser parity: a CUSTOM colour (not a preset — idx < 0) resets to the default (violet);
      // otherwise advance to the next preset, wrapping.
      next.accentColor = idx < 0 ? presets.front().key : presets[(idx + 1) % presets.size()].key;
      applySettings(next, true);   // apply + persist (re-themes everything, incl. the logo frame)
    });
    connect(logoBtn_, &QToolButton::clicked, this, [this] {
      // The click that dismissed a popover is spent doing exactly that.
      if (pop_.dismissClick) { pop_.dismissClick = false; return; }
      logoClickTimer_->start(250);
    });
    // Accent picker = a first-class popover (execMaybePopover); registering the
    // logo in pop_.buttons gives it the shared Alt machinery. The logo keeps
    // its own click/dblclick gestures (excluded from popover presses in eventFilter).
    actAccent_ = new QAction(tr("Theme Color"), this);
    actAccent_->setObjectName("actAccent");
    connect(actAccent_, &QAction::triggered, this, [this] { openAccentPicker(); });
    pop_.buttons.insert(logoBtn_, actAccent_);
    // Right-click: the same popover, STICKY — the popover icons' right-click route
    // (makeToolSection): a deliberate open, so the Alt release never closes it.
    // QToolButton::clicked never fires for the right button, so opening it can never
    // arm the cycle timer above.
    logoBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(logoBtn_, &QToolButton::customContextMenuRequested, this, [this] {
      if (pop_.clickTimer) pop_.clickTimer->stop();
      if (logoClickTimer_) logoClickTimer_->stop();
      pop_.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
      stopLingerPoll();         // a lingering window's poll must not close THIS open
      pop_.anchor = logoBtn_;
      actAccent_->trigger();
    });
    logoBtn_->installEventFilter(this);   // catch double-click → custom colour picker (see eventFilter)
    // Hover fx (LogoHoverFx above): the browser's logo pulse/levitate/glow/ray loop.
    // Purely visual — the click-cycle and dblclick-picker gestures above are untouched.
    logoFx_ = new LogoHoverFx(
        logoBtn_, [this] { return makeLogoPixmap(kHeaderLogo); },
        [this] {
          const QColor a = accentPrimary(settings_.accentColor);
          return a.isValid() ? a : QColor("#7c3aed");
        });
    headerToolbar_->addWidget(logoBtn_);
    // "Controls" chevron pill — collapses/expands the tool rows (routes through actToolbars_ so the
    // View-menu entry + Alt+C hotkey stay in sync). Icon (chevron) themed in styleActionIcons.
    // ControlsPill paints its own chevron + label: a stock icon+text QToolButton reserves
    // ~36px for the icon slot however small the chevron, leaving a wide gap beside the
    // label.
    controlsPill_ = new ControlsPill(this);
    controlsPill_->setObjectName("controlsPill");   // outlined pill, styled in theme.cpp
    // Its chevron's angle is STATE (toolbars shown/hidden), not hover feedback — the
    // browser's `[id^="toggle-"]` icon-motion opt-out.
    controlsPill_->setProperty(kNoIconMotionProperty, true);
    controlsPill_->setProperty(kShimmerRadiusProperty, 12);   // its QSS radius (shimmerOverlay.hpp)
    static_cast<ControlsPill*>(controlsPill_)->setLabel("Controls");
    // Capped, so the pill is never stretched to the header row the logo now makes tall —
    // a QToolBar filled it to 41px with a big rounded border ("huge border").
    controlsPill_->setMaximumHeight(28);
    controlsPill_->setAutoRaise(true);
    controlsPill_->setCursor(Qt::PointingHandCursor);
    controlsPill_->setToolTip(QString("Show / hide the toolbars (%1)").arg(hotkey("toggleControls", "Alt+C")));
    connect(controlsPill_, &QToolButton::clicked, this, [this] { if (actToolbars_) actToolbars_->toggle(); });
    headerToolbar_->addWidget(controlsPill_);
    headerToolbar_->addSeparator();
    buildProjectNameGroup(headerToolbar_);
    // "Image Size: W × H px" readout. Created here but placed in its own full-width bar
    // BELOW the toolbars (see buildImageInfoBar) — browser parity with the #image-info bar,
    // left-aligned above the canvas rather than tucked in the top-right corner.
    imageSizeInfo_ = new QLabel(this);
    imageSizeInfo_->setStyleSheet("color:#9aa0a8;");
    // contentsMargins, not stylesheet `padding` — QLabel's sizeHint()/paint don't reliably
    // pick it up. 10px left/right (browser parity: css/layout.css .info padding: 10px);
    // 11px top/bottom, so the readout sits in a band of its own rather than pressed
    // between the toolbars and the canvas.
    imageSizeInfo_->setContentsMargins(10, 11, 10, 11);
    imageSizeInfo_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    addToolBarBreak();
  }

}  // namespace stencil::gui

// buildMainToolbar()'s first row: the always-visible header (logo, Controls pill, project name).
// addToolBarBreak() ends it.
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconSet.hpp"
#include "LogoHoverFx.hpp"
#include "ControlsPill.hpp"
#include "SearchCombo.hpp"
#include "OpenImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"
#include "../support/iconMotion.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/WrapRow.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace stencil::gui {

  void MainWindow::buildHeaderRow() {
    // Stays put while the tool rows slide, like the browser's header keeping the "⌃ Controls" pill
    // + title.
    headerToolbar_ = addToolBar("Header");
    headerToolbar_->setObjectName("headerToolbar");  // named for QMainWindow::saveState
    headerToolbar_->setMovable(false);
    // Clicking the logo cycles the accent preset (browser parity).
    logoBtn_ = new QToolButton(this);
    logoBtn_->setCursor(Qt::PointingHandCursor);
    logoBtn_->setIconSize(QSize(HEADER_LOGO, HEADER_LOGO));
    // Size the button to the mark: a QToolBar lays an added widget out at its default icon metric
    // (~40px). The margin leaves the hover fx room.
    logoBtn_->setFixedSize(HEADER_LOGO + 6, HEADER_LOGO + 6);
    logoBtn_->setIcon(QIcon(makeLogoPixmap(HEADER_LOGO)));
    // LogoHoverFx paints the resting mark and blanks this icon — QToolButton draws it at half size
    // on Retina.
    logoBtn_->setToolTip(QString());   // no tooltip on the logo
    logoBtn_->setStyleSheet("QToolButton{border:none;background:transparent;padding:2px;}");
    // Deferred so a double-click can pre-empt it and open the picker (browser logo parity).
    logoClickTimer_ = new QTimer(this);
    logoClickTimer_->setSingleShot(true);
    connect(logoClickTimer_, &QTimer::timeout, this, [this] {
      const auto& presets = accentPresets();
      if (presets.empty()) return;
      int idx = -1;
      for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].key == settings_.accentColor) { idx = static_cast<int>(i); break; }
      auto next = settings_;
      // Browser parity: a custom colour (idx < 0) resets to the default; else the next preset,
      // wrapping.
      next.accentColor = idx < 0 ? presets.front().key : presets[(idx + 1) % presets.size()].key;
      applySettings(next, true);   // apply + persist (re-themes everything, incl. the logo frame)
    });
    connect(logoBtn_, &QToolButton::clicked, this, [this] {
      if (pop_.dismissClick) { pop_.dismissClick = false; return; }
      logoClickTimer_->start(250);
    });
    // Registering the logo in pop_.buttons gives it the shared Alt machinery; its own
    // click/dblclick stay excluded in eventFilter.
    actAccent_ = new QAction(tr("Theme Color"), this);
    actAccent_->setObjectName("actAccent");
    connect(actAccent_, &QAction::triggered, this, [this] { openAccentPicker(); });
    pop_.buttons.insert(logoBtn_, actAccent_);
    // Right-click opens the popover sticky; QToolButton::clicked never fires for the right button,
    // so it cannot arm the cycle timer.
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
    // LogoHoverFx: the browser's logo pulse/levitate/glow/ray loop, purely visual.
    logoFx_ = new LogoHoverFx(
        logoBtn_, [this] { return makeLogoPixmap(HEADER_LOGO); },
        [this] {
          const QColor a = accentPrimary(settings_.accentColor);
          return a.isValid() ? a : QColor("#7c3aed");
        });
    headerToolbar_->addWidget(logoBtn_);
    // Routes through actToolbars_ so the View entry + Alt+C stay in sync. ControlsPill paints its
    // own chevron + label: a stock icon+text QToolButton reserves ~36px for the icon slot.
    controlsPill_ = new ControlsPill(this);
    controlsPill_->setObjectName("controlsPill");   // outlined pill, styled in theme.cpp
    // The chevron's angle is state, not hover feedback — the browser's `[id^="toggle-"]` icon-
    // motion opt-out.
    controlsPill_->setProperty(NO_ICON_MOTION_PROPERTY, true);
    controlsPill_->setProperty(SHIMMER_RADIUS_PROPERTY, 12);   // its QSS radius (ShimmerOverlay.hpp)
    static_cast<ControlsPill*>(controlsPill_)->setLabel("Controls");
    // Capped, or a QToolBar fills the pill to the logo's row height with a huge border.
    controlsPill_->setMaximumHeight(28);
    controlsPill_->setAutoRaise(true);
    controlsPill_->setCursor(Qt::PointingHandCursor);
    controlsPill_->setToolTip(QString("Show / hide the toolbars (%1)").arg(hotkey("toggleControls", "Alt+C")));
    connect(controlsPill_, &QToolButton::clicked, this, [this] { if (actToolbars_) actToolbars_->toggle(); });
    headerToolbar_->addWidget(controlsPill_);
    headerToolbar_->addSeparator();
    buildProjectNameGroup(headerToolbar_);
    // Created here, placed in its own bar below the toolbars (buildImageInfoBar; browser #image-
    // info).
    imageSizeInfo_ = new QLabel(this);
    imageSizeInfo_->setStyleSheet("color:#9aa0a8;");
    // contentsMargins, not QSS padding, which QLabel's sizeHint ignores; 10px sides (browser .info
    // padding), 11px top/bottom.
    imageSizeInfo_->setContentsMargins(10, 11, 10, 11);
    imageSizeInfo_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    addToolBarBreak();
  }

}  // namespace stencil::gui

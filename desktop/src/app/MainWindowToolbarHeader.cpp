// buildMainToolbar()'s first row: the always-visible header (logo, Controls pill, project name).
// addToolBarBreak() ends it.
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconSet.hpp"
#include "LogoHoverFx.hpp"
#include "LogoStage.hpp"
#include "Notifications.hpp"
#include "ChatPlanTarget.hpp"
#include "../support/logoStageRules.hpp"
#include "../support/toastShine.hpp"
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
          return a.isValid() ? a : QColor(DEFAULT_ACCENT_HEX);
        });
    buildLogoStage();
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

  namespace {
    // The heart the pink show draws: a closed, filled line over the picture's centre square.
    core::Line heartLine(const QSize& size) {
      const support::LogoStageConfig& cfg = support::logoStageConfig();
      core::Line line;
      line.locked = true;
      line.color = cfg.heartStroke.toStdString();
      line.fillColor = cfg.heartFill.toStdString();
      line.thickness = cfg.heartThickness;
      for (const QPointF& p : support::heartPoints(size.width(), size.height()))
        line.points.push_back(core::Point{p.x(), p.y()});
      return line;
    }
  }  // namespace

  // The stage's seam onto the window (browser js/ui/logoStageTrigger.js): it holds no
  // MainWindow, only what these hooks hand it.
  void MainWindow::buildLogoStage() {
    LogoStage::Hooks hooks;
    hooks.makeMark = [this](int size) { return makeLogoPixmap(size); };
    hooks.accent = [this] {
      const QColor a = accentPrimary(settings_.accentColor);
      return a.isValid() ? a : QColor(DEFAULT_ACCENT_HEX);
    };
    hooks.accentKey = [this] { return settings_.accentColor; };
    hooks.bareWindow = [this] {
      return !fs_.active && !QApplication::activeModalWidget() &&
             !QApplication::activePopupWidget() && !pop_.active;
    };
    hooks.toast = [this](const QString& text) {
      if (!notify_) return;
      notify_->show(text, Notifications::Level::SUCCESS, 3000, /*special=*/true);
      support::installToastShine(notify_->lastToast());
    };
    // Through the same appliers a toolbar click and a script op take.
    hooks.pinkVibe = [this] {
      const support::LogoStageConfig& cfg = support::logoStageConfig();
      ChatPlanTarget target(*this);
      if (!target.hasImage()) {
        QString err;
        target.newBlank(cfg.pinkBlank.name(), QString(), 0, 0, &err);
      }
      if (!target.hasImage()) return;
      target.setImageFilter(QStringLiteral("custom"), cfg.pinkTint.name());
      core::Lines lines = canvas_->lines();
      lines.push_back(heartLine(canvas_->image().size()));
      target.commitLayoutLines(lines);   // one step on the user's own undo stack
      fitToWindow();   // the heart is the show — a page taller than the viewport hides it
    };
    hooks.stopClick = [this] { if (logoClickTimer_) logoClickTimer_->stop(); };
    // LogoHoverFx re-raises itself on hover, so it would paint over the stage; it stands
    // down for the show and paints its resting mark again afterwards.
    hooks.coverChrome = [this](bool on) {
      showCovered_ = on;
      if (logoFx_) asLogoFx(logoFx_)->standDown(on);
      // The dock edges re-raise themselves on every layout pass, so a resize during a show would
      // put them back over it; they stand down with the header mark and come back with it.
      positionPanelGrip();
      positionChatEdge();
    };
    hooks.hideNotices = [this](bool on) {
      for (const QString& n : {QStringLiteral("toast"), QStringLiteral("toastShine")})
        for (QWidget* w : findChildren<QWidget*>(n)) w->setVisible(!on);
    };
    new LogoStage(this, logoBtn_, std::move(hooks));
  }

}  // namespace stencil::gui

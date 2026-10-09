// buildMainToolbar()'s first row: the always-visible header (logo, Controls pill, project name).
// addToolBarBreak() ends it.
#include "MainWindow.hpp"
#include "windowSheets.hpp"
#include "ToolbarBuilder.hpp"
#include "ToastStack.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "LogoDrag.hpp"
#include "LogoLineAims.hpp"
#include "SelectedLineBar.hpp"
#include "SelectionPanel.hpp"
#include "LogoHoverFx.hpp"
#include "LogoStage.hpp"
#include "Notifications.hpp"
#include "ChatPlanTarget.hpp"
#include "../../support/motion/toastShine.hpp"
#include "ControlsPill.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/tip/altPeek.hpp"
#include "../../support/uiTimings.hpp"
#include "iconMotionTypes.hpp"
#include "tipContent.hpp"
#include "theme.hpp"
#include <QDialog>
#include <QScrollArea>
#include <QToolBar>

namespace stencil::gui {

  void ToolbarBuilder::buildHeaderRow() {
    // Stays put while the tool rows slide, like the browser's header keeping the "⌃ Controls" pill
    // + title.
    w.tools.headerToolbar = w.editor->addToolBar("Header");
    w.tools.headerToolbar->setObjectName("headerToolbar");  // named for QMainWindow::saveState
    w.tools.headerToolbar->setMovable(false);
    // Clicking the logo cycles the accent preset (browser parity).
    w.tools.logoBtn = new QToolButton(&w);
    w.tools.logoBtn->setCursor(Qt::PointingHandCursor);
    w.tools.logoBtn->setIconSize(QSize(HEADER_LOGO, HEADER_LOGO));
    // Size the button to the mark: a QToolBar lays an added widget out at its default icon metric
    // (~40px). The margin leaves the hover fx room.
    w.tools.logoBtn->setFixedSize(HEADER_LOGO + 6, HEADER_LOGO + 6);
    w.tools.logoBtn->setIcon(QIcon(w.parts.theme.makeLogoPixmap(HEADER_LOGO)));
    // LogoHoverFx paints the resting mark and blanks this icon — QToolButton draws it at half size
    // on Retina.
    w.tools.logoBtn->setToolTip(QString());   // no tooltip on the logo
    w.tools.logoBtn->setStyleSheet(support::logoButtonSheet());
    // Deferred so a double-click can pre-empt it and open the picker (browser logo parity).
    w.tools.logoClickTimer = new QTimer(&w);
    w.tools.logoClickTimer->setSingleShot(true);
    QObject::connect(w.tools.logoClickTimer, &QTimer::timeout, &w, [this] {
      const auto& presets = accentPresets();
      if (presets.empty()) return;
      int idx = -1;
      for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].key == w.settings.accentColor) { idx = static_cast<int>(i); break; }
      auto next = w.settings;
      // Browser parity: a custom colour (idx < 0) resets to the default; else the next preset,
      // wrapping.
      next.accentColor = idx < 0 ? presets.front().key : presets[(idx + 1) % presets.size()].key;
      w.applySettings(next, true);   // apply + persist (re-themes everything, incl. the logo frame)
    });
    QObject::connect(w.tools.logoBtn, &QToolButton::clicked, &w, [this] {
      if (w.pop.dismissClick) { w.pop.dismissClick = false; return; }
      w.tools.logoClickTimer->start(support::uiTimings().doubleClickMs);
    });
    // Registering the logo in pop.buttons gives it the shared Alt machinery; its own
    // click/dblclick stay excluded in eventFilter.
    w.acts.accent = new QAction(MainWindow::tr("Theme Color"), &w);
    w.acts.accent->setObjectName("actAccent");
    QObject::connect(w.acts.accent, &QAction::triggered, &w, [this] { w.parts.theme.openAccentPicker(); });
    w.pop.buttons.insert(w.tools.logoBtn, w.acts.accent);
    w.tools.logoBtn->setProperty(support::ALT_PEEK_TARGET_PROPERTY, true);
    // Right-click opens the popover sticky; QToolButton::clicked never fires for the right button,
    // so it cannot arm the cycle timer.
    w.tools.logoBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(w.tools.logoBtn, &QToolButton::customContextMenuRequested, &w, [this] {
      if (w.pop.clickTimer) w.pop.clickTimer->stop();
      if (w.tools.logoClickTimer) w.tools.logoClickTimer->stop();
      w.pop.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
      w.parts.popoverGestures.stopLingerPoll();         // a lingering window's poll must not close THIS open
      w.pop.anchor = w.tools.logoBtn;
      w.acts.accent->trigger();
    });
    // LogoHoverFx: the browser's logo pulse/levitate/glow/ray loop, purely visual.
    w.tools.logoFx = new LogoHoverFx(
        w.tools.logoBtn, [this] { return w.parts.theme.makeLogoPixmap(HEADER_LOGO); },
        [this] {
          const QColor a = accentPrimary(w.settings.accentColor);
          return a.isValid() ? a : QColor(DEFAULT_ACCENT_HEX);
        });
    buildLogoStage();
    w.tools.headerToolbar->addWidget(w.tools.logoBtn);
    // Routes through acts.toolbars so the View entry + Alt+C stay in sync. ControlsPill paints its
    // own chevron + label: a stock icon+text QToolButton reserves ~36px for the icon slot.
    w.tools.controlsPill = new ControlsPill(&w);
    w.tools.controlsPill->setObjectName("controlsPill");   // outlined pill, styled in theme.cpp
    // The chevron's angle is state, not hover feedback — the browser's `[id^="toggle-"]` icon-
    // motion opt-out.
    w.tools.controlsPill->setProperty(NO_ICON_MOTION_PROPERTY, true);
    w.tools.controlsPill->setProperty(SHIMMER_RADIUS_PROPERTY, 12);   // its QSS radius (ShimmerOverlay.hpp)
    static_cast<ControlsPill*>(w.tools.controlsPill)->setLabel("Controls");
    // Capped, or a QToolBar fills the pill to the logo's row height with a huge border.
    w.tools.controlsPill->setMaximumHeight(28);
    w.tools.controlsPill->setAutoRaise(true);
    w.tools.controlsPill->setCursor(Qt::PointingHandCursor);
    setTipBase(w.tools.controlsPill, "Hide controls");   // browser #toggle-controls; the chord is the keycap
    setTipHotkey(w.tools.controlsPill, w.acts.toolbars);
    QObject::connect(w.tools.controlsPill, &QToolButton::clicked, &w, [this] { if (w.acts.toolbars) w.acts.toolbars->toggle(); });
    w.tools.headerToolbar->addWidget(w.tools.controlsPill);
    w.tools.headerToolbar->addSeparator();
    buildProjectNameGroup(w.tools.headerToolbar);
    // Created here, placed in its own bar below the toolbars (buildImageInfoBar; browser #image-
    // info).
    w.tools.imageSizeInfo = new QLabel(&w);
    w.parts.theme.restyleImageSizeInfo();
    // contentsMargins, not QSS padding, which QLabel's sizeHint ignores; 10px sides (browser .info
    // padding), 11px top/bottom.
    w.tools.imageSizeInfo->setContentsMargins(10, 11, 10, 11);
    w.tools.imageSizeInfo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Its own widget, so it can come and go in the selected motion; the size line's right
    // margin is the gap in front of it (browser: the .hints-incognito span).
    w.tools.incognitoTag = new QLabel(&w);
    w.tools.incognitoTag->setTextFormat(Qt::RichText);
    w.tools.incognitoTag->setContentsMargins(0, 11, 10, 11);
    w.tools.incognitoTag->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    w.tools.incognitoTag->hide();
    w.editor->addToolBarBreak();
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

  // The stage's seam onto the window (browser js/ui/logo/stageTrigger.js): it holds no
  // MainWindow, only what these hooks hand it.
  void ToolbarBuilder::buildLogoStage() {
    LogoStage::Hooks hooks;
    hooks.makeMark = [this](int size) { return w.parts.theme.makeLogoPixmap(size); };
    hooks.accent = [this] {
      const QColor a = accentPrimary(w.settings.accentColor);
      return a.isValid() ? a : QColor(DEFAULT_ACCENT_HEX);
    };
    hooks.accentKey = [this] { return w.settings.accentColor; };
    hooks.bareWindow = [this] {
      return !w.fs.active && !QApplication::activeModalWidget() &&
             !QApplication::activePopupWidget() && !w.pop.active;
    };
    hooks.clearWay = [this] {
      if (QWidget* menu = QApplication::activePopupWidget()) menu->close();
      QWidget* modal = QApplication::activeModalWidget();
      if (w.pop.active) w.dismissPopover();
      else if (auto* dlg = qobject_cast<QDialog*>(modal)) dlg->reject();
      else if (modal) modal->close();
      if (w.fs.active) w.parts.view.toggleFullscreen();
    };
    hooks.toast = [this](const QString& text) {
      if (!w.notify) return;
      w.notify->show(text, Notifications::Level::SUCCESS, 3000, /*special=*/true);
      support::installToastShine(w.notify->toasts()->lastToast());
    };
    // Through the same appliers a toolbar click and a script op take.
    hooks.pinkVibe = [this] {
      const support::LogoStageConfig& cfg = support::logoStageConfig();
      ChatPlanTarget target(w);
      if (!target.hasImage()) {
        QString err;
        target.newBlank(cfg.pinkBlank.name(), QString(), 0, 0, &err);
      }
      if (!target.hasImage()) return;
      w.applyTintColor(cfg.pinkTint, /*asUndoStep=*/false);   // the heart's step carries the tint
      w.applyImageFilter(QStringLiteral("custom"), false);
      core::Lines lines = w.canvas->getLines();
      lines.push_back(heartLine(w.canvas->getImage().size()));
      target.commitLayoutLines(lines);   // one step on the user's own undo stack
      w.fitToWindow();   // the heart is the show — a page taller than the viewport hides it
    };
    hooks.webcore = [this] { return w.parts.theme.toggleWebcore(); };
    hooks.stopClick = [this] { if (w.tools.logoClickTimer) w.tools.logoClickTimer->stop(); };
    // LogoHoverFx re-raises itself on hover, so it would paint over the stage; it stands
    // down for the show and paints its resting mark again afterwards.
    hooks.coverChrome = [this](bool on) {
      w.showCovered = on;
      if (w.tools.logoFx) asLogoFx(w.tools.logoFx)->standDown(on);
      // The dock edges re-raise themselves on every layout pass, so a resize during a show would
      // put them back over it; they stand down with the header mark and come back with it.
      w.parts.dockChrome.positionPanelGrip();
      w.parts.dockChrome.positionChatEdge();
    };
    hooks.hideNotices = [this](bool on) {
      for (const QString& n : {QStringLiteral("toast"), QStringLiteral("toastShine")})
        for (QWidget* widget : w.findChildren<QWidget*>(n)) widget->setVisible(!on);
    };
    const LogoStage* stage = new LogoStage(&w, w.tools.logoBtn, std::move(hooks));
    LogoDragHooks drag;
    drag.mark = [this] { return w.parts.theme.makeLogoPixmap(HEADER_LOGO); };
    drag.free = [this] { return !QApplication::activeModalWidget() && !w.pop.active && !w.pop.dismissClick; };
    drag.canvas = [this]() -> QWidget* { return w.scroll; };
    drag.hasImage = [this] { return w.canvas->hasImage(); };
    drag.preview = [this](bool on) { w.canvas->setCleanPreview(on); };
    drag.commit = [this] {
      if (w.settings.imageFilter != QLatin1String("none")) w.applyImageFilter(QStringLiteral("none"));
      for (QAction* view : {w.acts.showLines, w.acts.showPoints})
        if (view->isChecked()) view->setChecked(false);
      if (w.canvas->getCompareMode() != QLatin1String("none"))
        w.parts.styleControls.setCompareModeUi(QStringLiteral("none"));
    };
    addLogoLineHooks(drag, {w.canvas, w.selPanel, w.selectedLineBar, w.scroll, &w.settings});
    installLogoDrag(w.tools.logoBtn, stage, std::move(drag));
  }

}  // namespace stencil::gui

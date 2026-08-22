#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "iconSet.hpp"
#include "incognitoOverlay.hpp"
#include "logoHoverFx.hpp"
#include "notifications.hpp"
#include "projectDragZones.hpp"
#include "selectionPanel.hpp"
#include "theme.hpp"
#include "tipContent.hpp"
#include "../support/themeSwapOverlay.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QLayout>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

// MainWindow's theming: applyTheme() and the icon/section restyling passes.
// Split from mainWindow.cpp; same class, definitions only.

namespace stencil::gui {

  // ── theme + settings ──
  void MainWindow::applyTheme() {
    // Tri-state resolution (S14): system follows the OS scheme.
    const bool dark = resolveDark(settings_.themeMode);
    // A real palette change gets the browser's flood-from-the-centre wipe: snapshot the
    // window as it looks now, restyle, then erase the snapshot with a growing circle.
    // Every route lands here (Settings dialog, logo click/cycle, the LLM setAccent op, a
    // .stencil file's embedded theme, the OS flipping under "system"), so hooking the one
    // apply covers them all — and skipping the boot pass and the no-op re-applies.
    const bool swapping = themePainted_ && (dark != paintedDark_ || settings_.accentColor != paintedAccent_);
    // Start the wipe at the ICON that owns the change — the theme button for a
    // light/dark flip, the logo for an accent cycle — exactly as the browser blooms
    // from its theme toggle. The cursor is NOT good enough: driving this from the View
    // menu leaves the pointer up at the menu bar, near the screen's top-left, so the
    // circle appeared to come out of the window corner.
    ThemeSwapOverlay* wipe = nullptr;
    if (swapping) {
      const bool themeFlipped = dark != paintedDark_;
      QWidget* anchor = themeFlipped ? buttonForAction(actTheme_) : nullptr;
      if (!anchor) anchor = themeFlipped ? logoBtn_ : static_cast<QWidget*>(logoBtn_);
      QPoint origin(-1, -1);
      if (anchor && anchor->isVisible())
        origin = anchor->mapTo(this, anchor->rect().center());
      else {
        const QPoint c = mapFromGlobal(QCursor::pos());   // last resort: the pointer
        if (rect().contains(c)) origin = c;
      }
      wipe = ThemeSwapOverlay::capture(this, origin);
      themeWipe_ = wipe;
    }
    themePainted_ = true;
    paintedDark_ = dark;
    paintedAccent_ = settings_.accentColor;
    // Apply at the application level so menus, popups and native chrome (which
    // aren't children of this window) are themed too. With the Fusion style set
    // in main(), a matching palette + stylesheet themes the whole app — on
    // Fedora a widget-level setStyleSheet left the menubar/toolbar unthemed.
    qApp->setPalette(buildQPalette(dark, settings_.accentColor));
    qApp->setStyleSheet(buildStylesheet(dark, settings_.accentColor));
    // Tooltips are rendered as rich text (tipContent.hpp) — their keycaps and muted lines
    // are literal colours, so they have to be re-taken from the palette on every swap.
    setTooltipPalette(themePalette(dark, settings_.accentColor));
    canvas_->setDark(dark);
    canvas_->setAccent(settings_.accentColor);
    incognitoOverlay_->setTheme(dark, settings_.accentColor);
    if (dropZones_) dropZones_->setAccent(themePalette(dark, settings_.accentColor).accent);
    actTheme_->setText(dark ? "Light Theme" : "Dark Theme");

    // Re-tint the shared line-art icons to the active text color (light/dark/accent).
    const QColor iconCol = themePalette(dark, settings_.accentColor).textMain;
    styleActionIcons(dark, iconCol);
    retintMenuIconsForSystem(dark, iconCol);
    if (selPanel_) selPanel_->restyleIcons(iconCol);
    // The colour chips carry a palette-coloured frame (updateColorSwatch), so they are
    // re-issued from HERE — after the snapshot — like every other themed control.
    if (lineColorBtn_) updateColorSwatch(lineColorBtn_, lineColorValue_);
    if (pointColorBtn_) updateColorSwatch(pointColorBtn_, effectiveDefaultPointColor());
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, filterColorValue_);
    if (blankColorBtn_ && blankColorBtn_->isVisible()) {
      const QColor blank(blankColor_);
      updateColorSwatch(blankColorBtn_, blank.isValid() ? blank : QColor("#ffffff"));
    }
    if (chatDock_) chatDock_->restyleIcons(themePalette(dark, settings_.accentColor));
    // The context-menu assistant row tracks the theme too (its rows/composer
    // are palette-tinted; the menu chrome around it is styled app-wide).
    if (chatMenuPanel_)
      asChatMenu(chatMenuPanel_)->restyle(themePalette(dark, settings_.accentColor));
    if (logoBtn_) logoBtn_->setIcon(QIcon(makeLogoPixmap(kHeaderLogo)));   // frame tracks the accent colour
    if (logoFx_) asLogoFx(logoFx_)->themeChanged();   // mid-hover accent cycle: fx keeps the pixels
    positionOverlayArrows();   // re-tint the Controls-pill chevron + the panel re-open tab
    sizeViewToggles();
    // Toasts take this theme's --accent (anything routine) and --danger (failures).
    if (notify_)
      notify_->setColors(themePalette(dark, settings_.accentColor).accent,
                         themePalette(dark, settings_.accentColor).danger);

    QPalette vp;
    vp.setColor(QPalette::Window, themePalette(dark).bgPage);
    scroll_->viewport()->setAutoFillBackground(true);
    scroll_->viewport()->setPalette(vp);

    // Everything above is painted — now wipe the old snapshot away over the top of it.
    if (wipe) wipe->start();
  }

  // A checkbox sizes itself against Qt's DEFAULT indicator, but theme.cpp's QSS draws a wider
  // one — and applying that stylesheet re-polishes the widget, wiping any floor set earlier. So
  // it is re-applied after every applyTheme, from the QSS's own numbers (16px indicator + 1px
  // border a side + 7px spacing) plus the label; keep the two in step if that QSS changes.
  // Without it the View section under-hints and the layout laid "Points" UNDER the next box.
  void MainWindow::sizeViewToggles() {
    for (QCheckBox* box : {showPointsCheck_, showLinesCheck_}) {
      if (!box) continue;
      // Trailing padding, applied through the STYLESHEET so it reaches the widget's sizeHint
      // (a plain setMinimumWidth does not, and the row kept squeezing them). The QSS indicator
      // is wider than the metrics Qt laid the row out with, so each box overruns its cell by a
      // few px; the padding is what that overrun eats instead of the neighbour's label.
      box->setStyleSheet(QStringLiteral("padding-right:12px;"));
      // The rows above already laid out at the old hint, so re-run each from the section up.
      for (QWidget* w = box->parentWidget(); w && w != this; w = w->parentWidget()) {
        if (!w->layout()) continue;
        w->layout()->invalidate();
        w->layout()->activate();
      }
    }
  }
  // macOS renders the menu bar in the SYSTEM appearance, so when it disagrees
  // with the app theme, re-tint the ACTIONS for the system and push app-themed
  // icons back onto the toolbar BUTTONS. No-op elsewhere / when they agree.
  void MainWindow::retintMenuIconsForSystem(bool appDark, const QColor& appIconColor) {
#ifdef Q_OS_MACOS
    const bool sysDark = systemPrefersDark();
    if (sysDark == appDark) return;   // nothing to reconcile
    const QColor menuCol = themePalette(sysDark, settings_.accentColor).textMain;
    const int s = kToolIcon;
    // Every menu glyph takes the menu text colour — destructive ones included
    // (their red lives on the toolbar button's fill, see styleActionIcons).
    for (auto it = actionIconNames_.constBegin(); it != actionIconNames_.constEnd(); ++it) {
      if (it.key()) it.key()->setIcon(themedIcon(it.value(), menuCol, s));
    }
    // …then give the toolbar buttons their app-themed icons back. Done AFTER the
    // actions, since a QToolButton mirrors its default action's icon on every change.
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      const auto name = actionIconNames_.constFind(a);
      if (name != actionIconNames_.constEnd())
        b->setIcon(themedIcon(name.value(), toolButtonIconColor(a, appIconColor), s));
    }
#else
    Q_UNUSED(appDark);
    Q_UNUSED(appIconColor);
#endif
  }

  // Map every action + icon toolbutton to a shared-icon glyph rasterized in
  // `iconColor` (names mirror browser/js/ui/toolbar.js). Null-guarded.
  void MainWindow::styleActionIcons(bool dark, const QColor& iconColor) {
    iconColor_ = iconColor;
    const int s = kToolIcon;
    // Remember each action's glyph so the menu-bar pass below can re-tint just those
    // without repeating this whole mapping.
    auto set = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames_.insert(a, QString::fromLatin1(name));
      dangerIcons_.remove(a);
    };
    // Destructive actions carry the NEUTRAL menu glyph, exactly like the browser:
    // its context-menu rows paint every .ctx-icon in --text-muted, and the red of
    // `.danger` buttons (#clear-all-lines, #clear-storage) is the BUTTON's fill,
    // under a white glyph — never a red mark on a plain menu background. The
    // dangerIcons_ membership still drives that fill (styleDangerToolButtons).
    auto setDanger = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames_.insert(a, QString::fromLatin1(name));
      dangerIcons_.insert(a);
    };
    // File / image
    set(actOpen_, "image");
    set(actLinks_, "link");
    set(actConnect_, "server");
    set(actOpenIn_, "monitor");
    set(actCrop_, "crop");
    set(actRotateLeft_, "rotate-ccw");
    set(actRotateRight_, "rotate-cw");
    set(actCycleFilter_, "image");
    // Drawing / history
    set(actStartDraw_, "play");
    set(actStopDraw_, "stop");
    set(actNewLine_, "plus");
    set(actUndo_, "undo");
    // Toolbar clusters ported from the browser (View / Data / Settings): these were
    // menu-only before, so they had no glyph.
    // Not the trash can: that is "delete the project/file" (actClearProject_ below).
    setDanger(actClearAll_, "eraser");
    set(actDownloadJson_, "download");
    set(actCopyLayout_, "copy");
    set(actUploadJson_, "upload");
    setDanger(actClearProject_, "trash");
    set(actSettings_, "gear");
    set(actInfo_, "help");
    set(actRedo_, "redo");
    set(actDeleteLast_, "minus");
    setDanger(actDeleteLine_, "trash");
    set(actDeletePoint_, "x");
    set(actDeselect_, "x");
    // View / zoom
    set(actZoomIn_, "plus");
    set(actZoomOut_, "minus");
    set(actFit_, "fit");
    // Show Points / Show Lines are checkable toggles: leave them icon-less so the menu renders
    // its native check-mark for the on state (browser contextMenu.js parity — a check when shown,
    // nothing when hidden). An icon here would take the check column and mask the on/off state.
    if (actShowPoints_) actShowPoints_->setIcon(QIcon());
    if (actShowLines_) actShowLines_->setIcon(QIcon());
    // Arrow toggles (browser parity): a chevron to collapse the points panel (→, it's on the right)
    // and the toolbars (↑). The panel chevron flips ←/→ with its shown state in refreshActions.
    set(actPanel_, actPanel_ && actPanel_->isChecked() ? "chevron-right" : "chevron-left");
    set(actToolbars_, "chevron-up");   // top-menu (toolbars) show/hide, View menu only
    set(actChat_, "sparkle");          // AI Assistant chat dock (browser sparkle parity)
    set(actFullscreen_, "maximize");
    set(actTooltip_, "message");
    set(actAllowFormulas_, "function");
    set(actUnitCm_, "ruler");
    set(actUnitIn_, "ruler");
    // Incognito: always the mask glyph (browser parity — the browser keeps the same icon and
    // just dims it when disabled). Qt auto-greys the icon for the disabled/locked state, so we
    // don't swap in a separate lock glyph.
    if (actIncognito_) actIncognito_->setIcon(themedIcon("incognito", iconColor, s));
    set(actSettings_, "gear");
    set(actAccent_, "palette");   // Settings section: the accent/visuals popover
    // Project / data
    set(actProjects_, "layers");          // browser projects-btn glyph (layers, not folder)
    set(actNewProject_, "file-text");
    set(actSaveProject_, "save");
    set(actSaveProjectFile_, "save");     // Projects toolbar: Save Project (.stencil)
    set(actOpenProjectFile_, "folder");   // Projects toolbar: Open Project (.stencil)
    set(actStencilLiveSync_, "refresh");  // Projects toolbar: live sync to file
    // The two DESTRUCTIVE ones keep the danger tint set above — a plain set() here ran
    // last and quietly repainted them in the normal icon colour, so the trash that
    // removes your project looked like any other button (browser: .btn-danger red).
    setDanger(actDeleteProjectFile_, "trash");
    setDanger(actClearProject_, "trash");
    set(actSaveSession_, "clipboard");
    set(actDownloadJson_, "download");
    set(actUploadJson_, "upload");
    set(actCopyLayout_, "copy");
    set(actPasteLayout_, "paste");
    set(actSaveImage_, "download");        // browser save-image glyph (download)
    set(actCopyImage_, "copy");
    set(actPasteImage_, "paste");
    // Help
    set(actInfo_, "info");
    set(actShortcuts_, "help");
    set(actQuit_, "power");
    // Context-menu extras
    set(actDrawModeToggle_, "rect");   // browser contextMenu.js parity (rect outline, not a pencil)
    set(actDrawRectNow_, "rect-filled");
    // The theme toggle shows the destination scheme (sun when dark, moon when light),
    // matching the browser's toggle glyph.
    if (actTheme_) actTheme_->setIcon(themedIcon(dark ? "sun" : "moon", iconColor, s));

    // Toolbuttons that aren't backed by a QAction. The rename confirm/cancel mirror the browser's
    // green ✓ / red ✗ inline-edit buttons.
    if (projectNameAccept_)
      projectNameAccept_->setIcon(themedIcon("check", QColor("#2e9e4f"), 16));
    if (projectNameCancel_)
      projectNameCancel_->setIcon(themedIcon("x", QColor("#d6293e"), 16));
    // Browser-style name affordances: a ✎ rename pencil + a 🎨 colour icon (flat line-art glyphs
    // following the theme text colour — not a filled swatch).
    if (projectNameEdit_) projectNameEdit_->setIcon(themedIcon("pencil", iconColor, 15));
    if (projectColorBtn_) projectColorBtn_->setIcon(themedIcon("palette", iconColor, 15));
    // blankColorBtn_'s icon is a live colour swatch (set in updateProjectTitle), not a themed glyph.
    if (drawModeBtn_) {
      const bool rect =
          canvas_ && canvas_->drawMode() == CanvasWidget::DrawMode::Rect;
      drawModeBtn_->setIcon(
          themedIcon(rect ? "rect-filled" : "pencil", iconColor, 16));
    }
    styleDangerToolButtons();          // filled-red trash buttons (browser .danger parity)
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
    if (chatUnread_) setChatUnread(true);   // repaint the mark in the new accent
  }

  // The glyph colour a TOOLBAR button wants for `act`. Destructive actions sit on a solid
  // danger fill there (QToolButton[dangerFill]), so their glyph is white — the neutral one
  // the MENUS use would vanish into the red.
  QColor MainWindow::toolButtonIconColor(QAction* act, const QColor& normal) const {
    for (QToolButton* b : findChildren<QToolButton*>())
      if (b->defaultAction() == act && !b->property("toolFill").toString().isEmpty())
        return QColor(Qt::white);
    return dangerIcons_.contains(act) ? QColor(Qt::white) : normal;
  }

  // Filled-danger treatment for destructive toolbar buttons — the ONLY place the
  // danger red appears (menus keep the neutral glyph). Must run again once the
  // toolbars exist: styleActionIcons can fire before any button exists.
  void MainWindow::styleDangerToolButtons() {
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // Only the toolbar-section buttons take a fill; makeToolSection tags them with the
      // section they belong to. Settings stays a bordered ghost, as in the browser.
      const QVariant sect = b->property("toolSection");
      if (!sect.isValid()) continue;
      // No fill for the Settings ghosts, for a button tagged toolGhost (Fit to window), nor
      // for a CHECKABLE toggle: on those the accent means "on" (browser #chat-btn /
      // .active), so it comes from QToolButton:checked.
      const QString fill = (sect.toString() == QLatin1String("Settings")
                            || b->property("toolGhost").toBool() || a->isCheckable())
                               ? QString()
                               : (dangerIcons_.contains(a) ? QStringLiteral("danger")
                                                           : QStringLiteral("accent"));
      b->setProperty("toolFill", fill);
      const auto paint = [this, a, b] {
        const auto name = actionIconNames_.constFind(a);
        if (name != actionIconNames_.constEnd())
          b->setIcon(themedIcon(name.value(), toolButtonIconColor(a, iconColor_), kToolIcon));
      };
      paint();
      // A QToolButton re-copies its default action's icon on every QEvent::ActionChanged —
      // so the first setEnabled/setVisible from refreshActions put the MENU's glyph back on
      // the fill, where it is invisible. Qt sends that event before it emits changed(), so
      // repainting from this signal lands last. Connected once per button.
      if (!b->property("fillSync").toBool()) {
        b->setProperty("fillSync", true);
        connect(a, &QAction::changed, b, paint);
      }
      // Qt matches property selectors at POLISH time, so a property set after the
      // stylesheet was applied changes nothing until the widget is re-polished.
      b->style()->unpolish(b);
      b->style()->polish(b);
      b->update();
    }
  }

  // Recolour the context-menu hosted checkboxes/radios so their indicators use the theme TEXT
  // colour, matching the surrounding menu text rather than the app-wide accent (which the global
  // QSS applies to every other QCheckBox/QRadioButton). The check/dot glyphs are rasterised in
  // the text colour and cached on disk keyed by hex, so a theme switch regenerates them without
  // Qt serving a stale QSS-image cache. Applied per-widget so only these menu controls change.
  void MainWindow::restyleContextToggles(const QColor& textColor) {
    const QString hex = textColor.name().mid(1);  // "rrggbb"
    const QString checkPath = QDir::tempPath() + "/stencil-ctx-check-" + hex + ".png";
    const QString dotPath = QDir::tempPath() + "/stencil-ctx-dot-" + hex + ".png";
    if (!QFileInfo::exists(checkPath))
      themedIcon("check", textColor, 12).pixmap(12, 12).save(checkPath, "PNG");
    if (!QFileInfo::exists(dotPath)) {
      QPixmap dot(12, 12);
      dot.fill(Qt::transparent);
      {
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(textColor);
        p.drawEllipse(3, 3, 6, 6);
      }  // painter destroyed before save
      dot.save(dotPath, "PNG");
    }
    const QString css =
        QStringLiteral(
            "QCheckBox::indicator,QRadioButton::indicator{width:15px;height:15px;"
            "border:1px solid %1;background:transparent;}"
            "QCheckBox::indicator{border-radius:4px;}"
            "QRadioButton::indicator{border-radius:8px;}"
            "QCheckBox::indicator:checked{image:url(\"%2\");}"
            "QRadioButton::indicator:checked{image:url(\"%3\");}")
            .arg(textColor.name(), checkPath, dotPath);
    QList<QWidget*> toggles = {tooltipEnableCheck_, ttPageCheck_, ttScreenCheck_,
                               ttCoordsCheck_, ctxAllowFormulas_};
    if (filterButtons_)
      for (QAbstractButton* b : filterButtons_->buttons()) toggles.append(b);
    for (QWidget* w : toggles)
      if (w) w->setStyleSheet(css);
  }

}  // namespace stencil::gui

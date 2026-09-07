#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "../support/dockGrip.hpp"
#include "dropZonesOverlay.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "iconSet.hpp"
#include "incognitoOverlay.hpp"
#include "logoHoverFx.hpp"
#include "notifications.hpp"
#include "projectDragZones.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "theme.hpp"
#include "overlayScrollArea.hpp"
#include "tipContent.hpp"
#include "../support/faceSwap.hpp"
#include "../support/motionPrefs.hpp"   // support::dustAllowed()
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
      // The circle kicks up dust in its wake, painted in the palette it is erasing —
      // read from the PAINTED state, before the restyle below moves it. Only in the
      // particle mode: 'slide' keeps the wipe and drops its grain (browser parity —
      // motion.js spawnSwapDust is gated the same way).
      if (wipe && support::dustAllowed()) {
        const Palette old = themePalette(paintedDark_, paintedAccent_);
        wipe->seedDust(old.bgPage, old.textMain, old.accent);
      }
      themeWipe_ = wipe;
    }
    // The app-wide palette/stylesheet depend only on (dark, accent): skip the global
    // re-polish (it restyles every widget in the process) when neither moved — the
    // live-apply Settings dialog runs this whole function per control click.
    const bool restyleApp = !themePainted_ || swapping;
    themePainted_ = true;
    paintedDark_ = dark;
    paintedAccent_ = settings_.accentColor;
    if (restyleApp) {
      // Apply at the application level so menus, popups and native chrome (which
      // aren't children of this window) are themed too. With the Fusion style set
      // in main(), a matching palette + stylesheet themes the whole app — on
      // Fedora a widget-level setStyleSheet left the menubar/toolbar unthemed.
      qApp->setPalette(buildQPalette(dark, settings_.accentColor));
      qApp->setStyleSheet(buildStylesheet(dark, settings_.accentColor));
    }
    // Tooltips are rendered as rich text (tipContent.hpp) — their keycaps and muted lines
    // are literal colours, so they have to be re-taken from the palette on every swap.
    setTooltipPalette(themePalette(dark, settings_.accentColor));
    // The canvas scrollbars paint their own thumbs (overlayScrollArea.hpp) — hand them
    // the theme's colours, since a stylesheet cannot round them.
    if (scroll_)
      static_cast<OverlayScrollArea*>(scroll_)->setThumbColors(
          canvasScrollThumb(dark), canvasScrollThumbHover(dark, settings_.accentColor));
    canvas_->setDark(dark);
    canvas_->setAccent(settings_.accentColor);
    incognitoOverlay_->setTheme(dark, settings_.accentColor);
    if (dropZones_) dropZones_->setAccent(themePalette(dark, settings_.accentColor).accent);
    // The canvas↔panel separator grip paints in palette colours of its own.
    if (panelGrip_ || chatEdge_) {
      const Palette gp = themePalette(dark, settings_.accentColor);
      if (panelGrip_) panelGrip_->setColors(gp.borderMain, gp.accent);
      if (chatEdge_) chatEdge_->setAccent(gp.accent);   // …and the chat dock's resize edge
    }
    actTheme_->setText(dark ? "Light Theme" : "Dark Theme");

    // Re-tint the shared line-art icons to the active text color (light/dark/accent).
    const QColor iconCol = themePalette(dark, settings_.accentColor).textMain;
    styleActionIcons(dark, iconCol);
    // styleActionIcons() just reset actCopyImage_/actSaveImage_ to their resting-state
    // glyph — re-apply the split-compare override (if any) on top of it.
    syncSplitCopyDownloadSlot();
    retintMenuIconsForSystem(dark, iconCol);
    if (selPanel_) selPanel_->restyleIcons(iconCol);
    if (selectedLineBar_) selectedLineBar_->restyleIcons(iconCol);
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

    // The drop-hint's lightbulb is a rasterised glyph (inline <img> can't take the
    // stylesheet's color), so it's re-tinted here like every other themed icon.
    if (dropHintIcon_)
      dropHintIcon_->setPixmap(themedIcon("lightbulb", themePalette(dark, settings_.accentColor).textMuted, 14)
                                    .pixmap(14, 14));

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
      // A toggle that paints its own face (support/faceSwap.hpp) is re-synced below, not
      // repainted from the action — its glyph colour is its STATE, not the theme text.
      if (b->property(kFaceGlyphProperty).isValid()) continue;
      const auto name = actionIconNames_.constFind(a);
      if (name != actionIconNames_.constEnd())
        b->setIcon(themedIcon(name.value(), toolButtonIconColor(a, appIconColor), s));
    }
    syncDrawToggleFace(canvas_ && canvas_->isDrawing(), false);
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
    set(actOpenAnother_, "external");
    set(actLinks_, "link");
    set(actDescription_, "description");
    set(actKeywords_, "keywords");
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
    set(actSettings_, "palette");
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
    set(actSettings_, "palette");
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
    // actSaveImage_/actCopyImage_ always mean "Current" — the toolbar's own generic glyph,
    // whichever variant they perform (browser parity: exportOptionsMenu.js VARIANT_ICONS —
    // "'current' keeps whichever action icon the caller passes").
    set(actSaveImage_, "download");        // browser save-image glyph (download)
    set(actCopyImage_, "copy");
    set(actSaveImageCurrentRow_, "download");   // "Current"'s own row — same glyph as the primary
    set(actCopyImageCurrentRow_, "copy");
    // The split siblings and the other fixed variants each get their OWN glyph — not the
    // trigger's icon repeated (browser parity: contextMenu.js copyImg-sub/dlImg-sub icons).
    // syncSplitCopyDownloadSlot() is re-run right after this pass (applyTheme) so a theme
    // swap mid-compare doesn't matter — visibility, not icon, is all it still touches here.
    set(actSaveImageSplit_, "compare");
    set(actCopyImageSplit_, "compare");
    set(actCopyImageTint_, "palette");
    set(actSaveImageOriginal_, "image");
    set(actSaveImageTint_, "palette");
    set(actCopyImageOriginal_, "image");
    set(actShareImage_, "share");
    set(actPasteImage_, "paste");
    // Help — matches the browser's icon('help')/icon('gear') pair (js/ui/toolbar.js
    // settings-btn/info-btn): the gear opens Shortcuts, the question mark opens Help.
    set(actInfo_, "help");
    set(actShortcuts_, "gear");
    set(actQuit_, "power");
    // Context-menu extras — the same animated outline glyphs as the toolbar's Line/Rect
    // face (browser contextMenu.js parity: ctx-draw-line/ctx-draw-rect, icon('line')/('rect')).
    set(actDrawLineNow_, "line");
    set(actDrawRectNow_, "rect");
    // The theme toggle shows the destination scheme (sun when dark, moon when light),
    // matching the browser's toggle glyph.
    if (actTheme_) actTheme_->setIcon(themedIcon(dark ? "sun" : "moon", iconColor, s));

    // Toolbuttons that aren't backed by a QAction. The rename confirm/cancel mirror the browser's
    // green ✓ / red ✗ inline-edit buttons.
    if (projectNameAccept_)
      projectNameAccept_->setIcon(themedIcon("check", QColor("#2e9e4f"), 16));
    if (projectNameCancel_)
      projectNameCancel_->setIcon(themedIcon("x", QColor("#d6293e"), 16));
    // Browser-style name affordances: a ✎ rename pencil + a 🎨 colour icon, flat line-art
    // glyphs in the theme text colour. Each carries a white twin in QIcon::Active (the mode
    // Qt paints an auto-raise button in on hover), so the glyph turns white as the chip
    // fills with the accent. A QIcon is the only way in — QSS cannot recolour an icon.
    const auto affordanceIcon = [&](const char* glyph) {
      QIcon ic = themedIcon(glyph, iconColor, 15);
      ic.addPixmap(themedIcon(glyph, QColor("#ffffff"), 15).pixmap(15, 15), QIcon::Active);
      return ic;
    };
    if (projectNameEdit_) projectNameEdit_->setIcon(affordanceIcon("pencil"));
    if (projectColorBtn_) projectColorBtn_->setIcon(affordanceIcon("palette"));
    // blankColorBtn_'s icon is a live colour swatch (set in updateProjectTitle), not a themed glyph.
    // Both Draw toggles own their own glyph (support/faceSwap.hpp), so they are repainted
    // through their face — instantly, this is a theme change and not a toggle.
    syncDrawModeFace(canvas_ && canvas_->drawMode() == CanvasWidget::DrawMode::Rect, false);
    styleDangerToolButtons();          // filled-red trash buttons (browser .danger parity)
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
    if (chatUnread_) setChatUnread(true);   // repaint the mark in the new accent
  }

  // ── the two Draw toggles' faces ──
  // Start ▶ / Stop ■. The FUNCTIONAL half lands at once — which action a click fires, the
  // tooltip and shortcut it carries, whether it is enabled — while the face (glyph + word)
  // and the accent state cross over through the shared swap. Idle is the OUTLINED accent
  // (accent glyph and word on a neutral face); drawing is the filled one, whose foreground
  // is the app's on-accent white plus the light-accent halo, exactly as chatDock's filled
  // accent buttons pick theirs. Browser parity: #draw-toggle / .active in layout.css.
  void MainWindow::syncDrawToggleFace(bool drawing, bool animate) {
    if (!startDrawBtn_ || !actStartDraw_ || !actStopDraw_) return;
    QAction* want = drawing ? actStopDraw_ : actStartDraw_;
    const bool flipped = startDrawBtn_->defaultAction() != want;
    // A swap already heading for this face owns the button until it lands — refreshActions
    // runs on all sorts of things, and none of them should cut a toggle short.
    if (!flipped && animate && faceSwapping(startDrawBtn_)) return;
    if (flipped) startDrawBtn_->setDefaultAction(want);   // icon/tooltip/enabled/click target
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    FaceSpec face;
    face.glyph = drawing ? QStringLiteral("stop") : QStringLiteral("play");
    face.label = want->iconText();   // the short toolbar word; the menus keep the long one
    face.iconSize = kToolIcon;
    face.glyphColor = drawing ? QColor(Qt::white) : pal.accent;
    face.textColor = face.glyphColor;
    face.halo = drawing && accentNeedsGlyphShadow(pal.accent);
    // The fill flip is hidden at the swap's pivot, where the face is invisible. It SETS the
    // state (never toggles it), so a superseded swap can be dropped without stranding it.
    auto applyFill = [this, drawing] {
      startDrawBtn_->setProperty("drawToggle", drawing ? QStringLiteral("on")
                                                       : QStringLiteral("idle"));
      startDrawBtn_->style()->unpolish(startDrawBtn_);
      startDrawBtn_->style()->polish(startDrawBtn_);
    };
    swapFace(startDrawBtn_, face, applyFill, animate && flipped ? kFaceSwapMs : 0);
  }

  // Line ✎ / Rect ▭ — the same swap, no accent STATE of its own (it picks the mode, it
  // does not report a live session) but a permanent accent FILL: it has no idle/on pair
  // to distinguish the way Start/Stop does, and the plain toolbutton ghost (transparent,
  // a translucent tint only on hover) left it looking unstyled next to every other
  // action button in the row, which the browser's `#draw-mode-toggle` never is — a bare
  // `<button>` there, so it is solid accent-filled at rest too (layout.css `button {}`).
  // Port of drawingApp.js syncDrawModeUI.
  void MainWindow::syncDrawModeFace(bool rect, bool animate) {
    if (!drawModeBtn_) return;
    FaceSpec face;
    // SIBLINGS, not two families: an outlined rectangle beside a line-with-endpoint-dots,
    // both 2px strokes on the same grid, both anchoring the SAME two drag handles. Both
    // now live in the shared canon (browser/js/config/icons.json), so they carry the
    // motion hooks too and draw themselves on hover (iconMotion.json).
    face.glyph = rect ? QStringLiteral("rect") : QStringLiteral("line");
    face.label = rect ? QStringLiteral("Rect") : QStringLiteral("Line");
    face.iconSize = 16;   // a touch under kToolIcon: this glyph reads heavier than the rest
    // On-accent white, like every other filled toolbar button (toolButtonIconColor) —
    // plus the light-accent halo those pick up too, for the same contrast reason.
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    face.glyphColor = QColor(Qt::white);
    face.textColor = QColor(Qt::white);
    face.halo = accentNeedsGlyphShadow(pal.accent);
    drawModeBtn_->setToolTip(rect ? "Drawing mode: Rectangle (click to switch to Line)"
                                  : "Drawing mode: Line (click to switch to Rectangle)");
    const bool flipped =
        drawModeBtn_->property(kFaceLabelProperty).toString() != face.label;
    swapFace(drawModeBtn_, face, {}, animate && flipped ? kFaceSwapMs : 0);
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
      // The Start/Stop toggle opts OUT of the section fill: its accent says which state it
      // is in (QToolButton[drawToggle]), so a permanent accent chip would say nothing.
      // Both actions have to restore its face, since either one's enable/disable re-copies
      // that action's menu glyph onto the button.
      if (b == startDrawBtn_) {
        b->setProperty("toolFill", QString());
        if (!b->property("fillSync").toBool()) {
          b->setProperty("fillSync", true);
          // A REPAINT, not a transition: the state change itself comes through
          // refreshActions and gets the swap, and this must not pre-empt it (the
          // enable/disable that starts a session fires first).
          for (QAction* state : {actStartDraw_, actStopDraw_})
            connect(state, &QAction::changed, b, [b] { repaintFace(b); });
        }
        syncDrawToggleFace(canvas_ && canvas_->isDrawing(), false);
        b->style()->unpolish(b);
        b->style()->polish(b);
        b->update();
        continue;
      }
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
        // The compound [toolFill="danger"]:disabled selector needs a re-polish on every
        // enabled/disabled flip, same as the property itself does below — otherwise a
        // destructive action that goes disabled (Clear All Lines with nothing to clear)
        // kept its solid red fill instead of falling back to the muted disabled chip.
        b->style()->unpolish(b);
        b->style()->polish(b);
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

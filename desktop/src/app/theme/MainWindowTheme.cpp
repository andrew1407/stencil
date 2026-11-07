// MainWindow's theming: applyTheme() and the restyling passes that follow a theme, accent or skin change, each
// tint read off one palette built for the pass; the skin's answers come through the installed look strategy.
// Browser twin: ui/bindings/theme.js and the sessions under ui/skin/.
#include "MainWindow.hpp"
#include "../../support/skin/SkinLookStrategy.hpp"
#include "../../support/skinPrefs.hpp"
#include "ProjectTitleController.hpp"
#include "ChatSessionController.hpp"
#include "ToastStack.hpp"
#include "toolbarMetrics.hpp"
#include "CanvasWidget.hpp"
#include "../../support/dockGrip.hpp"
#include "DropZonesOverlay.hpp"
#include "ChatMenuPanel.hpp"
#include "ScriptMenuPanel.hpp"
#include "iconSet.hpp"
#include "IncognitoOverlay.hpp"
#include "LogoHoverFx.hpp"
#include "Notifications.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "theme.hpp"
#include "PillScrollBars.hpp"
#include "tipContent.hpp"
#include "../../support/motionPrefs.hpp"
#include "../../support/dust/ThemeSwapOverlay.hpp"
#include "../../support/modal/ModalBackdrop.hpp"

#include <QCursor>
#include <QScrollArea>
#include <QToolButton>

namespace stencil::gui {

  namespace {
    struct AppLook {
      bool dark = false;
      support::Skin skin = support::Skin::DEFAULT;
      QString accent;
      QString sheet;
    };

    // `isFresh`: a window's first paint re-polishes the widgets it built before it, whatever is applied.
    void restyleAppOnce(bool dark, const QString& accent, bool isFresh) {
      static AppLook applied;
      const support::Skin skin = support::skin();
      if (!isFresh && !applied.sheet.isEmpty() && applied.dark == dark && applied.skin == skin &&
          applied.accent == accent && qApp->styleSheet() == applied.sheet)
        return;
      QString sheet = support::skinLook().stylesheet(dark, accent);
      if (sheet.isEmpty()) sheet = buildStylesheet(dark, accent);
      applied = AppLook{dark, skin, accent, sheet};
      qApp->setPalette(buildQPalette(dark, accent));
      qApp->setStyleSheet(applied.sheet);
    }
  }  // namespace

  void MainWindow::applyTheme() {
    const bool dark = parts.theme.paintingDark();
    const QString accent = settings.accentColor;
    support::setSkinDark(dark);   // before any icon is asked for: the skin's art has two faces
    // A real palette change gets the browser's flood-from-the-centre wipe. Never under reduced
    // motion, and never while one is in flight - stacking snapshots tore the window.
    const bool paletteMoved = dark != painted.dark || accent != painted.accent;
    // Under CLI mode the stage paints the Zig console's own wipe, so no flood goes over it.
    const bool swapping = painted.done && !support::motionReduced() && !painted.swapping() && paletteMoved &&
                          !painted.silent && !parts.console.cliModeOn();
    // Start the wipe at the icon that owns the change, as the browser blooms from its toggle; the
    // cursor may be up at the menu bar.
    ThemeSwapOverlay* wipe = nullptr;
    QList<QPointer<ThemeSwapOverlay>> wipes;
    if (swapping) {
      const bool themeFlipped = dark != painted.dark;
      QWidget* anchor = themeFlipped ? buttonForAction(acts.theme) : nullptr;
      if (!anchor) anchor = tools.logoBtn;
      QPoint origin(-1, -1);
      if (anchor && anchor->isVisible())
        origin = anchor->mapTo(this, anchor->rect().center());
      else {
        const QPoint c = mapFromGlobal(QCursor::pos());   // last resort: the pointer
        if (rect().contains(c)) origin = c;
      }
      // Its open dialogs too; an accent alone pours from the top; the wake takes the palette being erased, read first.
      const Palette old = themePalette(painted.dark, painted.accent);
      wipes = ThemeSwapOverlay::captureWindows(this, origin, old.accent, old.textKey, painted.dark, !themeFlipped);
      painted.wipe = wipe = wipes.value(0);
    }
    // The global re-polish depends only on (dark, accent); keyed off paletteMoved, not `swapping`:
    // a change mid-wipe skips the wipe but must still restyle.
    const bool isFresh = !painted.done;
    const bool restyleApp = isFresh || paletteMoved;
    painted.done = true;
    painted.dark = dark;
    painted.accent = accent;
    // Application level so menus, popups and native chrome are themed too; a widget-level sheet
    // left the menubar unthemed on Fedora. Once per look app-wide: a sibling's echo finds it applied.
    if (restyleApp) restyleAppOnce(dark, accent, isFresh);
    // Tooltips are rich text with literal colours (tipContent.hpp), re-taken from the palette on
    // every swap.
    const Palette pal = themePalette(dark, accent);   // built once for the pass: every tint below reads it
    setTooltipPalette(pal);
    const auto [first, second] = support::skinLook().particleStops(accentPrimary(accent), pal, dark);
    support::setParticlePalette(first, second, dark);
    // An open accent popover keeps its ✓ on the applied accent, whichever route moved it.
    parts.theme.remarkAccentPopover();
    // Scrollbar thumbs are painted pills (support/control/PillScrollBars.hpp); a stylesheet cannot round
    // or accent them.
    ScrollBarPill::setColors(canvasScrollThumb(dark),
                             canvasScrollThumbHover(dark, accent));
    canvas->setDark(dark);
    canvas->setAccent(accent);
    overlays.incognito->setTheme(dark, accent);
    if (overlays.dropZones) overlays.dropZones->setColors(pal.accent, pal.textKey, pal.textMuted, pal.bgContainer);
    parts.console.onThemeApplied();
    if (parts.dockChrome.panelGrip || parts.dockChrome.chatEdge) {
      const auto [grip, lit] = support::skinLook().gripInk(pal, dark);
      if (parts.dockChrome.panelGrip) parts.dockChrome.panelGrip->setColors(grip, lit);
      if (parts.dockChrome.chatEdge) parts.dockChrome.chatEdge->setColors(grip, lit);
    }
    acts.theme->setText(dark ? "Light Theme" : "Dark Theme");

    const QColor iconCol = pal.textMain;
    parts.theme.styleActionIcons(dark, iconCol);
    // styleActionIcons() reset the copy/save glyphs; re-apply the split-compare override on top.
    parts.exportMenus.syncSplitCopyDownloadSlot();
    parts.theme.retintMenuIconsForSystem(dark, iconCol);
    if (selPanel) selPanel->restyleIcons(iconCol, pal.danger);
    if (selectedLineBar) selectedLineBar->restyleIcons(iconCol);
    // The colour chips' palette frame (updateColorSwatch) is re-issued after the snapshot like
    // every other themed control.
    if (tools.lineColorBtn) updateColorSwatch(tools.lineColorBtn, tools.lineColorValue);
    if (tools.pointColorBtn) updateColorSwatch(tools.pointColorBtn, parts.styleControls.effectiveDefaultPointColor());
    if (tools.filterColorBtn) updateColorSwatch(tools.filterColorBtn, tools.filterColorValue);
    if (nameBar.blankColorBtn && nameBar.blankColorBtn->isVisible()) {
      const QColor blank(projectHost.docSource.blankColor);
      updateColorSwatch(nameBar.blankColorBtn, blank.isValid() ? blank : QColor(Qt::white));
    }
    if (chatDock) chatDock->restyleIcons(pal);
    if (chatSession->chatMenuPanel)
      asChatMenu(chatSession->chatMenuPanel)->restyle(pal);
    if (ctxMenu.scriptPanel)
      asScriptMenu(ctxMenu.scriptPanel)->restyle(pal);
    if (tools.logoBtn) tools.logoBtn->setIcon(QIcon(parts.theme.makeLogoPixmap(HEADER_LOGO)));   // frame tracks the accent colour
    if (tools.logoFx) asLogoFx(tools.logoFx)->themeChanged();   // mid-hover accent cycle: fx keeps the pixels
    positionOverlayArrows();   // re-tint the Controls-pill chevron + the panel re-open tab
    parts.theme.sizeViewToggles();
    if (notify)
      notify->toasts()->setColors(pal.accent, pal.danger);

    QPalette vp;
    vp.setColor(QPalette::Window, pal.bgCanvas);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setPalette(vp);

    // The drop-hint's lightbulb is a rasterised glyph an inline <img> cannot recolour, and its
    // keycaps are painted pictures carrying literal colours, so the whole line is rebuilt.
    if (tools.dropHintIcon)
      tools.dropHintIcon->setPixmap(themedIcon("lightbulb", pal.textMuted, 14)
                                    .pixmap(14, 14));
    parts.theme.refreshDropHint();
    parts.theme.restyleImageSizeInfo();
    projectTitle->applyProjectNameStyle(false);
    // Settings' live accent/theme rows restyle a window its modal backdrop froze on open; with
    // nothing moved, re-photographing it only made the blurred backdrop blink.
    if (restyleApp)
      support::ModalBackdrop::retakeOn(this, wipe ? QList<QWidget*>{wipe} : QList<QWidget*>{});

    for (const QPointer<ThemeSwapOverlay>& fx : wipes) if (fx) fx->start();
  }

  // A toolbar glyph takes its ground's on-colour (theme.hpp onAccentInk, or white on danger red).
  QColor MainWindow::toolButtonIconColor(QAction* act, const QColor& normal) const {
    if (support::skinTraits().hasGlassToggles)   // glass carries the ink, a lit toggle's gel its own (qss/aqua/chrome.qss)
      return act && act->isChecked() && act->isEnabled() ? support::skinGlass().onGel : normal;
    for (QToolButton* b : findChildren<QToolButton*>()) {
      if (b->defaultAction() != act) continue;
      const QString fill = b->property("toolFill").toString();
      // A ghost toggle takes the accent's ink only while it is ON (qss/app/toolButtons.qss toolGhostBox:checked).
      const bool litGhost = b->property("toolGhostBox").toBool() && act->isChecked() && act->isEnabled();
      if (fill.isEmpty() && !litGhost) break;
      if (fill == QLatin1String("danger")) return QColor(Qt::white);
      return themePalette(resolveDark(settings.themeMode), settings.accentColor).onAccent;
    }
    return tools.dangerIcons.contains(act) ? QColor(Qt::white) : normal;
  }
}  // namespace stencil::gui

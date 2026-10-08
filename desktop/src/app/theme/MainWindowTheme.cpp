#include "../../support/skinPrefs.hpp"
#include "../../support/webcore/stylesheet.hpp"
#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include <QToolButton>
#include "ChatSessionController.hpp"
#include "ToastStack.hpp"
#include "mainWindowHelpers.hpp"
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
#include "../../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "../../support/dust/ThemeSwapOverlay.hpp"
#include "../../support/modal/ModalBackdrop.hpp"

#include <QCursor>
#include <QScrollArea>

// MainWindow's theming: applyTheme() and the restyling passes.

namespace stencil::gui {

  void MainWindow::applyTheme() {
    const bool dark = parts.theme.paintingDark();
    support::setSkinDark(dark);   // before any icon is asked for: the skin's art has two faces
    // A real palette change gets the browser's flood-from-the-centre wipe. Never under reduced
    // motion, and never while one is in flight - stacking snapshots tore the window.
    const bool paletteMoved = dark != painted.dark || settings.accentColor != painted.accent;
    const bool swapping = painted.done && !support::motionReduced() && !painted.swapping() && paletteMoved &&
                          !painted.silent;
    // Start the wipe at the icon that owns the change, as the browser blooms from its toggle; the
    // cursor may be up at the menu bar.
    ThemeSwapOverlay* wipe = nullptr;
    if (swapping) {
      const bool themeFlipped = dark != painted.dark;
      QWidget* anchor = themeFlipped ? buttonForAction(acts.theme) : nullptr;
      if (!anchor) anchor = themeFlipped ? tools.logoBtn : static_cast<QWidget*>(tools.logoBtn);
      QPoint origin(-1, -1);
      if (anchor && anchor->isVisible())
        origin = anchor->mapTo(this, anchor->rect().center());
      else {
        const QPoint c = mapFromGlobal(QCursor::pos());   // last resort: the pointer
        if (rect().contains(c)) origin = c;
      }
      wipe = ThemeSwapOverlay::capture(this, origin);
      // Wake particles take the accent and shade being erased, read before the restyle moves them;
      // 'slide' drops the grain (browser parity).
      if (wipe && support::isDustAllowed()) {
        const Palette old = themePalette(painted.dark, painted.accent);
        wipe->seedDust(old.accent, old.textKey, painted.dark);
      }
      painted.wipe = wipe;
    }
    // The global re-polish depends only on (dark, accent); keyed off paletteMoved, not `swapping`:
    // a change mid-wipe skips the wipe but must still restyle.
    const bool restyleApp = !painted.done || paletteMoved;
    painted.done = true;
    painted.dark = dark;
    painted.accent = settings.accentColor;
    if (restyleApp) {
      // Application level so menus, popups and native chrome are themed too; a widget-level sheet
      // left the menubar unthemed on Fedora.
      qApp->setPalette(buildQPalette(dark, settings.accentColor));
      qApp->setStyleSheet(support::isWebcore() ? support::buildWebcoreStylesheet(dark, settings.accentColor)
                                               : buildStylesheet(dark, settings.accentColor));
    }
    // Tooltips are rich text with literal colours (tipContent.hpp), re-taken from the palette on
    // every swap.
    setTooltipPalette(themePalette(dark, settings.accentColor));
    {
      const Palette np = themePalette(dark, settings.accentColor);
      // The skin repoints the accent at its navy; the particles keep the chosen colour and its
      // second stop (browser webcore/tokens.css --dust-accent / --dust-accent-2).
      if (support::isWebcore()) {
        const QColor a = accentPrimary(settings.accentColor);
        const QColor edge = dark ? QColor(Qt::white) : QColor(Qt::black);
        const double k = dark ? 0.78 : 0.86;
        const QColor b = QColor::fromRgbF(a.redF() * k + edge.redF() * (1 - k), a.greenF() * k + edge.greenF() * (1 - k),
                                          a.blueF() * k + edge.blueF() * (1 - k));
        support::setParticlePalette(a, b, dark);
      } else {
        support::setParticlePalette(np.accent, np.textKey, dark);
      }
    }
    // An open accent popover keeps its ✓ on the applied accent, whichever route moved it.
    parts.theme.remarkAccentPopover();
    // Scrollbar thumbs are painted pills (support/control/PillScrollBars.hpp); a stylesheet cannot round
    // or accent them.
    ScrollBarPill::setColors(canvasScrollThumb(dark),
                             canvasScrollThumbHover(dark, settings.accentColor));
    canvas->setDark(dark);
    canvas->setAccent(settings.accentColor);
    overlays.incognito->setTheme(dark, settings.accentColor);
    if (overlays.dropZones) {
      const Palette dp = themePalette(dark, settings.accentColor);
      overlays.dropZones->setColors(dp.accent, dp.textKey, dp.textMuted, dp.bgContainer);
    }
    if (parts.dockChrome.panelGrip || parts.dockChrome.chatEdge) {
      const Palette gp = themePalette(dark, settings.accentColor);
      // Webcore's grip is the ink on the dark face, the shadow on the light one, and never lit.
      const QColor grip = support::isWebcore() && dark ? gp.textMain : gp.borderMain;
      // The skin repoints gp.accent at its navy; a lit grip wears the chosen accent (--wc-focus).
      const QColor lit = support::isWebcore() ? gp.textKey : gp.accent;
      if (parts.dockChrome.panelGrip) parts.dockChrome.panelGrip->setColors(grip, lit);
      if (parts.dockChrome.chatEdge) parts.dockChrome.chatEdge->setColors(grip, lit);
    }
    acts.theme->setText(dark ? "Light Theme" : "Dark Theme");

    const QColor iconCol = themePalette(dark, settings.accentColor).textMain;
    parts.theme.styleActionIcons(dark, iconCol);
    // styleActionIcons() reset the copy/save glyphs; re-apply the split-compare override on top.
    parts.exportMenus.syncSplitCopyDownloadSlot();
    parts.theme.retintMenuIconsForSystem(dark, iconCol);
    if (selPanel) selPanel->restyleIcons(iconCol, themePalette(dark, settings.accentColor).danger);
    if (selectedLineBar) selectedLineBar->restyleIcons(iconCol);
    // The colour chips' palette frame (updateColorSwatch) is re-issued after the snapshot like
    // every other themed control.
    if (tools.lineColorBtn) updateColorSwatch(tools.lineColorBtn, tools.lineColorValue);
    if (tools.pointColorBtn) updateColorSwatch(tools.pointColorBtn, parts.styleControls.effectiveDefaultPointColor());
    if (tools.filterColorBtn) updateColorSwatch(tools.filterColorBtn, tools.filterColorValue);
    if (nameBar.blankColorBtn && nameBar.blankColorBtn->isVisible()) {
      const QColor blank(docSource.blankColor);
      updateColorSwatch(nameBar.blankColorBtn, blank.isValid() ? blank : QColor("#ffffff"));
    }
    if (chatDock) chatDock->restyleIcons(themePalette(dark, settings.accentColor));
    if (chatSession->chatMenuPanel)
      asChatMenu(chatSession->chatMenuPanel)->restyle(themePalette(dark, settings.accentColor));
    if (ctxMenu.scriptPanel)
      asScriptMenu(ctxMenu.scriptPanel)->restyle(themePalette(dark, settings.accentColor));
    if (tools.logoBtn) tools.logoBtn->setIcon(QIcon(parts.theme.makeLogoPixmap(HEADER_LOGO)));   // frame tracks the accent colour
    if (tools.logoFx) asLogoFx(tools.logoFx)->themeChanged();   // mid-hover accent cycle: fx keeps the pixels
    positionOverlayArrows();   // re-tint the Controls-pill chevron + the panel re-open tab
    parts.theme.sizeViewToggles();
    if (notify)
      notify->toasts()->setColors(themePalette(dark, settings.accentColor).accent,
                         themePalette(dark, settings.accentColor).danger);

    QPalette vp;
    vp.setColor(QPalette::Window, themePalette(dark).bgPage);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setPalette(vp);

    // The drop-hint's lightbulb is a rasterised glyph an inline <img> cannot recolour, and its
    // keycaps are painted pictures carrying literal colours, so the whole line is rebuilt.
    if (tools.dropHintIcon)
      tools.dropHintIcon->setPixmap(themedIcon("lightbulb", themePalette(dark, settings.accentColor).textMuted, 14)
                                    .pixmap(14, 14));
    parts.theme.refreshDropHint();
    parts.theme.restyleImageSizeInfo();
    projectTitle->applyProjectNameStyle(false);
    // Settings' live accent/theme rows restyle a window its modal backdrop froze on open; with
    // nothing moved, re-photographing it only made the blurred backdrop blink.
    if (restyleApp)
      support::ModalBackdrop::retakeOn(this, wipe ? QList<QWidget*>{wipe} : QList<QWidget*>{});

    if (wipe) wipe->start();
  }

  // A toolbar glyph takes its ground's on-colour (theme.hpp onAccentInk, or white on danger red).
  QColor MainWindow::toolButtonIconColor(QAction* act, const QColor& normal) const {
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

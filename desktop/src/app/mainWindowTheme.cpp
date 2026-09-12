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
#include "pillScrollBars.hpp"
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

  void MainWindow::applyTheme() {
    // Tri-state resolution: system follows the OS scheme.
    const bool dark = resolveDark(settings_.themeMode);
    // A real palette change gets the browser's flood-from-the-centre wipe: snapshot the
    // window as it looks now, restyle, then erase the snapshot with a growing circle.
    // Every route lands here (Settings dialog, logo click/cycle, the LLM setAccent op, a
    // .stencil file's embedded theme, the OS flipping under "system"), so hooking the one
    // apply covers them all — and skipping the boot pass and the no-op re-applies.
    // …and never under reduced motion: the browser's themeSwap applies the palette
    // outright there (motionReduced), and so does this — the wipe is motion too.
    // …and never while one is still in flight: a hover preview re-applies on every row,
    // and stacking a fresh snapshot over a running wipe tore the window (the hpp note).
    // The palette still restyles; only the extra wipe is skipped.
    const bool paletteMoved = dark != paintedDark_ || settings_.accentColor != paintedAccent_;
    const bool swapping = themePainted_ && !support::motionReduced() && !themeSwapping() && paletteMoved;
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
      // The circle kicks up particles in its wake, painted in the accent and shade it is
      // erasing — read from the PAINTED state, before the restyle below moves it. Only in
      // a particle mode: 'slide' keeps the wipe and drops its grain (browser parity).
      if (wipe && support::dustAllowed()) {
        const Palette old = themePalette(paintedDark_, paintedAccent_);
        wipe->seedDust(old.accent, old.textKey, paintedDark_);
      }
      themeWipe_ = wipe;
    }
    // The app-wide palette/stylesheet depend only on (dark, accent): skip the global
    // re-polish (it restyles every widget in the process) when neither moved — the
    // live-apply Settings dialog runs this whole function per control click. Keyed off
    // paletteMoved, NOT `swapping`: a change that lands while a wipe is still in flight
    // skips the extra wipe but MUST still restyle, or the palette silently stalls
    // (a preview flooding over a running one, or a live Settings change mid-wipe).
    const bool restyleApp = !themePainted_ || paletteMoved;
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
    // The two colours a cloud is painted in: the accent and its --accent-2 shade, which
    // Palette carries as textKey.
    {
      const Palette np = themePalette(dark, settings_.accentColor);
      support::setParticlePalette(np.accent, np.textKey, dark);
    }
    // An open accent popover keeps its ✓ on the accent now applied, whichever route
    // moved it (the logo's click-cycle under the open list, a row pick, the dialog).
    remarkAccentPopover();
    // Every scrollbar's thumb is a painted pill (support/pillScrollBars.hpp) — hand the
    // painter the theme's colours, since a stylesheet cannot round or accent them.
    ScrollBarPill::setColors(canvasScrollThumb(dark),
                             canvasScrollThumbHover(dark, settings_.accentColor));
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
    if (nameBar_.blankColorBtn && nameBar_.blankColorBtn->isVisible()) {
      const QColor blank(blankColor_);
      updateColorSwatch(nameBar_.blankColorBtn, blank.isValid() ? blank : QColor("#ffffff"));
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
}  // namespace stencil::gui


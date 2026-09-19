#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "../support/dockGrip.hpp"
#include "DropZonesOverlay.hpp"
#include "ChatDock.hpp"
#include "ChatMenuPanel.hpp"
#include "ScriptMenuPanel.hpp"
#include "iconSet.hpp"
#include "IncognitoOverlay.hpp"
#include "LogoHoverFx.hpp"
#include "Notifications.hpp"
#include "ProjectDragZones.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "theme.hpp"
#include "PillScrollBars.hpp"
#include "tipContent.hpp"
#include "../support/faceSwap.hpp"
#include "../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "../support/ThemeSwapOverlay.hpp"

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

// MainWindow's theming: applyTheme() and the restyling passes.

namespace stencil::gui {

  void MainWindow::applyTheme() {
    const bool dark = resolveDark(settings_.themeMode);
    // A real palette change gets the browser's flood-from-the-centre wipe: snapshot, restyle,
    // erase with a growing circle. Every route lands here.
    // Never under reduced motion (browser themeSwap parity: the wipe is motion too).
    // Never while one is in flight: stacking a fresh snapshot over a running wipe tore the window.
    // The palette still restyles.
    const bool paletteMoved = dark != paintedDark_ || settings_.accentColor != paintedAccent_;
    const bool swapping = themePainted_ && !support::motionReduced() && !themeSwapping() && paletteMoved;
    // Start the wipe at the icon that owns the change, as the browser blooms from its toggle; the
    // cursor may be up at the menu bar.
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
      // Wake particles take the accent and shade being erased, read before the restyle moves them;
      // 'slide' drops the grain (browser parity).
      if (wipe && support::isDustAllowed()) {
        const Palette old = themePalette(paintedDark_, paintedAccent_);
        wipe->seedDust(old.accent, old.textKey, paintedDark_);
      }
      themeWipe_ = wipe;
    }
    // The global re-polish depends only on (dark, accent); keyed off paletteMoved, not `swapping`:
    // a change mid-wipe skips the wipe but must still restyle.
    const bool restyleApp = !themePainted_ || paletteMoved;
    themePainted_ = true;
    paintedDark_ = dark;
    paintedAccent_ = settings_.accentColor;
    if (restyleApp) {
      // Application level so menus, popups and native chrome are themed too; a widget-level sheet
      // left the menubar unthemed on Fedora.
      qApp->setPalette(buildQPalette(dark, settings_.accentColor));
      qApp->setStyleSheet(buildStylesheet(dark, settings_.accentColor));
    }
    // Tooltips are rich text with literal colours (tipContent.hpp), re-taken from the palette on
    // every swap.
    setTooltipPalette(themePalette(dark, settings_.accentColor));
    {
      const Palette np = themePalette(dark, settings_.accentColor);
      support::setParticlePalette(np.accent, np.textKey, dark);
    }
    // An open accent popover keeps its ✓ on the applied accent, whichever route moved it.
    remarkAccentPopover();
    // Scrollbar thumbs are painted pills (support/PillScrollBars.hpp); a stylesheet cannot round
    // or accent them.
    ScrollBarPill::setColors(canvasScrollThumb(dark),
                             canvasScrollThumbHover(dark, settings_.accentColor));
    canvas_->setDark(dark);
    canvas_->setAccent(settings_.accentColor);
    incognitoOverlay_->setTheme(dark, settings_.accentColor);
    if (dropZones_) dropZones_->setAccent(themePalette(dark, settings_.accentColor).accent);
    if (panelGrip_ || chatEdge_) {
      const Palette gp = themePalette(dark, settings_.accentColor);
      if (panelGrip_) panelGrip_->setColors(gp.borderMain, gp.accent);
      if (chatEdge_) chatEdge_->setAccent(gp.accent);   // …and the chat dock's resize edge
    }
    actTheme_->setText(dark ? "Light Theme" : "Dark Theme");

    const QColor iconCol = themePalette(dark, settings_.accentColor).textMain;
    styleActionIcons(dark, iconCol);
    // styleActionIcons() reset the copy/save glyphs; re-apply the split-compare override on top.
    syncSplitCopyDownloadSlot();
    retintMenuIconsForSystem(dark, iconCol);
    if (selPanel_) selPanel_->restyleIcons(iconCol);
    if (selectedLineBar_) selectedLineBar_->restyleIcons(iconCol);
    // The colour chips' palette frame (updateColorSwatch) is re-issued after the snapshot like
    // every other themed control.
    if (lineColorBtn_) updateColorSwatch(lineColorBtn_, lineColorValue_);
    if (pointColorBtn_) updateColorSwatch(pointColorBtn_, effectiveDefaultPointColor());
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, filterColorValue_);
    if (nameBar_.blankColorBtn && nameBar_.blankColorBtn->isVisible()) {
      const QColor blank(blankColor_);
      updateColorSwatch(nameBar_.blankColorBtn, blank.isValid() ? blank : QColor("#ffffff"));
    }
    if (chatDock_) chatDock_->restyleIcons(themePalette(dark, settings_.accentColor));
    if (chatMenuPanel_)
      asChatMenu(chatMenuPanel_)->restyle(themePalette(dark, settings_.accentColor));
    if (scriptMenuPanel_)
      asScriptMenu(scriptMenuPanel_)->restyle(themePalette(dark, settings_.accentColor));
    if (logoBtn_) logoBtn_->setIcon(QIcon(makeLogoPixmap(HEADER_LOGO)));   // frame tracks the accent colour
    if (logoFx_) asLogoFx(logoFx_)->themeChanged();   // mid-hover accent cycle: fx keeps the pixels
    positionOverlayArrows();   // re-tint the Controls-pill chevron + the panel re-open tab
    sizeViewToggles();
    if (notify_)
      notify_->setColors(themePalette(dark, settings_.accentColor).accent,
                         themePalette(dark, settings_.accentColor).danger);

    QPalette vp;
    vp.setColor(QPalette::Window, themePalette(dark).bgPage);
    scroll_->viewport()->setAutoFillBackground(true);
    scroll_->viewport()->setPalette(vp);

    // The drop-hint's lightbulb is a rasterised glyph an inline <img> cannot recolour, so it is
    // re-tinted here — and its keycaps are painted pictures carrying literal colours, so the
    // whole line is rebuilt rather than restyled.
    if (dropHintIcon_)
      dropHintIcon_->setPixmap(themedIcon("lightbulb", themePalette(dark, settings_.accentColor).textMuted, 14)
                                    .pixmap(14, 14));
    refreshDropHint();

    if (wipe) wipe->start();
  }

  // The paste combo as KEYCAPS in this platform's own glyphs — ⌘V on a Mac, where Qt binds
  // a configured "Ctrl" to Command, so the caps must say Command too. Browser twin:
  // mainContent.js pasteKeys(). The caps are painted pictures holding literal colours, so
  // this runs again on every theme change rather than being styled.
  void MainWindow::refreshDropHint() {
    if (!dropHintText_) return;
    const QString combo =
        QKeySequence(hotkey("paste", "Ctrl+V")).toString(QKeySequence::NativeText);
    // A keycap is taller than the type beside it, and an inline image inflates the line
    // box downwards — which left the sentence riding a couple of pixels high. One
    // middle-aligned table row centres prose and caps against each other instead, the
    // way a rich tooltip's own row does (tipContent.cpp renderTip).
    dropHintText_->setText(
        QString("<table cellspacing=\"0\" cellpadding=\"0\"><tr>"
                "<td style=\"vertical-align: middle;\">Drag &amp; drop an <b>image</b> or "
                "<b>.json</b> anywhere on the window — or paste an image with&nbsp;</td>"
                "<td style=\"vertical-align: middle;\">%1</td></tr></table>")
            .arg(comboKeycapsHtml(combo, currentPalette())));
  }

  // theme.cpp's QSS draws a wider indicator than Qt's default, and applying it re-polishes the
  // floor away; numbers are the QSS's (16px + 1px border a side + 7px spacing) — keep in step.
  void MainWindow::sizeViewToggles() {
    for (QCheckBox* box : {showPointsCheck_, showLinesCheck_}) {
      if (!box) continue;
      // Through the stylesheet so it reaches sizeHint (setMinimumWidth does not); the padding eats
      // the indicator's overrun instead of the neighbour's label.
      box->setStyleSheet(QStringLiteral("padding-right:12px;"));
      for (QWidget* w = box->parentWidget(); w && w != this; w = w->parentWidget()) {
        if (!w->layout()) continue;
        w->layout()->invalidate();
        w->layout()->activate();
      }
    }
  }
}  // namespace stencil::gui


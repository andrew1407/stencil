#include "../../support/skinPrefs.hpp"
#include "../../support/webcore/stylesheet.hpp"
#include "MainWindow.hpp"
#include "ToastStack.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "../../support/dockGrip.hpp"
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
#include "../../support/theme/faceSwap.hpp"
#include "../../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "../../support/dust/ThemeSwapOverlay.hpp"
#include "../../support/modal/ModalBackdrop.hpp"

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
    const bool dark = support::forcedDark().value_or(resolveDark(settings.themeMode));
    support::setSkinDark(dark);   // before any icon is asked for: the skin's art has two faces
    // A real palette change gets the browser's flood-from-the-centre wipe. Never under reduced
    // motion, and never while one is in flight - stacking snapshots tore the window.
    const bool paletteMoved = dark != paintedDark || settings.accentColor != paintedAccent;
    const bool swapping = themePainted && !support::motionReduced() && !themeSwapping() && paletteMoved;
    // Start the wipe at the icon that owns the change, as the browser blooms from its toggle; the
    // cursor may be up at the menu bar.
    ThemeSwapOverlay* wipe = nullptr;
    if (swapping) {
      const bool themeFlipped = dark != paintedDark;
      QWidget* anchor = themeFlipped ? buttonForAction(actTheme) : nullptr;
      if (!anchor) anchor = themeFlipped ? logoBtn : static_cast<QWidget*>(logoBtn);
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
        const Palette old = themePalette(paintedDark, paintedAccent);
        wipe->seedDust(old.accent, old.textKey, paintedDark);
      }
      themeWipe = wipe;
    }
    // The global re-polish depends only on (dark, accent); keyed off paletteMoved, not `swapping`:
    // a change mid-wipe skips the wipe but must still restyle.
    const bool restyleApp = !themePainted || paletteMoved;
    themePainted = true;
    paintedDark = dark;
    paintedAccent = settings.accentColor;
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
    remarkAccentPopover();
    // Scrollbar thumbs are painted pills (support/PillScrollBars.hpp); a stylesheet cannot round
    // or accent them.
    ScrollBarPill::setColors(canvasScrollThumb(dark),
                             canvasScrollThumbHover(dark, settings.accentColor));
    canvas->setDark(dark);
    canvas->setAccent(settings.accentColor);
    incognitoOverlay->setTheme(dark, settings.accentColor);
    if (dropZones) {
      const Palette dp = themePalette(dark, settings.accentColor);
      dropZones->setColors(dp.accent, dp.textKey, dp.textMuted, dp.bgContainer);
    }
    if (panelGrip || chatEdge) {
      const Palette gp = themePalette(dark, settings.accentColor);
      // Webcore's grip is the ink on the dark face, the shadow on the light one, and never lit.
      const QColor grip = support::isWebcore() && dark ? gp.textMain : gp.borderMain;
      // The skin repoints gp.accent at its navy; a lit grip wears the chosen accent (--wc-focus).
      const QColor lit = support::isWebcore() ? gp.textKey : gp.accent;
      if (panelGrip) panelGrip->setColors(grip, lit);
      if (chatEdge) { chatEdge->setAccent(lit); chatEdge->setBase(gp.bgPage); }   // the page shows in the gap, as the browser's body does
    }
    actTheme->setText(dark ? "Light Theme" : "Dark Theme");

    const QColor iconCol = themePalette(dark, settings.accentColor).textMain;
    styleActionIcons(dark, iconCol);
    // styleActionIcons() reset the copy/save glyphs; re-apply the split-compare override on top.
    syncSplitCopyDownloadSlot();
    retintMenuIconsForSystem(dark, iconCol);
    if (selPanel) selPanel->restyleIcons(iconCol, themePalette(dark, settings.accentColor).danger);
    if (selectedLineBar) selectedLineBar->restyleIcons(iconCol);
    // The colour chips' palette frame (updateColorSwatch) is re-issued after the snapshot like
    // every other themed control.
    if (lineColorBtn) updateColorSwatch(lineColorBtn, lineColorValue);
    if (pointColorBtn) updateColorSwatch(pointColorBtn, effectiveDefaultPointColor());
    if (filterColorBtn) updateColorSwatch(filterColorBtn, filterColorValue);
    if (nameBar.blankColorBtn && nameBar.blankColorBtn->isVisible()) {
      const QColor blank(blankColor);
      updateColorSwatch(nameBar.blankColorBtn, blank.isValid() ? blank : QColor("#ffffff"));
    }
    if (chatDock) chatDock->restyleIcons(themePalette(dark, settings.accentColor));
    if (chatMenuPanel)
      asChatMenu(chatMenuPanel)->restyle(themePalette(dark, settings.accentColor));
    if (scriptMenuPanel)
      asScriptMenu(scriptMenuPanel)->restyle(themePalette(dark, settings.accentColor));
    if (logoBtn) logoBtn->setIcon(QIcon(makeLogoPixmap(HEADER_LOGO)));   // frame tracks the accent colour
    if (logoFx) asLogoFx(logoFx)->themeChanged();   // mid-hover accent cycle: fx keeps the pixels
    positionOverlayArrows();   // re-tint the Controls-pill chevron + the panel re-open tab
    sizeViewToggles();
    if (notify)
      notify->toasts()->setColors(themePalette(dark, settings.accentColor).accent,
                         themePalette(dark, settings.accentColor).danger);

    QPalette vp;
    vp.setColor(QPalette::Window, themePalette(dark).bgPage);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setPalette(vp);

    // The drop-hint's lightbulb is a rasterised glyph an inline <img> cannot recolour, and its
    // keycaps are painted pictures carrying literal colours, so the whole line is rebuilt.
    if (dropHintIcon)
      dropHintIcon->setPixmap(themedIcon("lightbulb", themePalette(dark, settings.accentColor).textMuted, 14)
                                    .pixmap(14, 14));
    refreshDropHint();
    restyleImageSizeInfo();
    applyProjectNameStyle(false);
    // Settings' live accent/theme rows restyle a window its modal backdrop froze on open.
    support::ModalBackdrop::retakeOn(this, wipe ? QList<QWidget*>{wipe} : QList<QWidget*>{});

    if (wipe) wipe->start();
  }

  // The paste combo as KEYCAPS in this platform's glyphs (browser twin: mainContent.js pasteKeys).
  // The caps are painted pictures holding literal colours, so this re-runs on every theme change.
  void MainWindow::refreshDropHint() {
    if (!dropHintText) return;
    const QString combo =
        QKeySequence(hotkey("paste", "Ctrl+V")).toString(QKeySequence::NativeText);
    // A keycap is taller than the type beside it and an inline image inflates the line box downwards.
    // One middle-aligned table row centres prose and caps, as a rich tooltip's row does (tipContent).
    dropHintText->setText(
        QString("<table cellspacing=\"0\" cellpadding=\"0\"><tr>"
                "<td style=\"vertical-align: middle;\">Drag &amp; drop an <b>image</b> or "
                "<b>.json</b> anywhere on the window — or paste an image with&nbsp;</td>"
                "<td style=\"vertical-align: middle;\">%1</td></tr></table>")
            .arg(comboKeycapsHtml(combo, currentPalette())));
  }

  // theme.cpp's QSS draws a wider indicator than Qt's default, and applying it re-polishes the
  // floor away; numbers are the QSS's (16px + 1px border a side + 7px spacing) — keep in step.
  void MainWindow::sizeViewToggles() {
    for (QCheckBox* box : {showPointsCheck, showLinesCheck}) {
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


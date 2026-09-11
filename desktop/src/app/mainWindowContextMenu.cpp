#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "stayOpenMenu.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "guiHelpers.hpp"
#include "menuHotkeys.hpp"
#include "menuReveal.hpp"
#include "menuShimmer.hpp"
#include "iconSet.hpp"
#include "linksDialog.hpp"
#include "projectsDialog.hpp"
#include "remoteSession.hpp"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QWidgetAction>

// The canvas context menu tree (contextMenu.js parity).

namespace stencil::gui {

  void MainWindow::showContextMenu(const QPoint& globalPos) {
    // No image, no menu — every entry acts on one, so an empty editor's menu is all
    // dead rows. The single gate for all three ways in (canvas right-click, the
    // backdrop's, Shift+F10), mirroring contextMenu.js's `if (!app.image) return`.
    if (!canvas_ || !canvas_->hasImage()) return;
    syncContextActions();

    // Build the menu tree. Order mirrors contextMenu.js inner() (~5-108):
    // Image/Layout · Fullscreen · Fit · — · Draw · DrawMode · DrawRect · — ·
    // Show Points/Lines · Clear · — · Style · Filter · Transformation · Tooltip.
    // StayOpenMenu keeps the menu open when a hosted checkbox/radio row is clicked (a plain QMenu
    // closes on release over a QWidgetAction). Submenus are StayOpenMenus too, for the same reason.
    StayOpenMenu menu(this);

    // Submenu-parent icons mirror contextMenu.js (folder / palette / image / function / message).
    // The menu is rebuilt per right-click, so the icons are (re)applied here in the current theme's
    // icon colour.
    const int subIcon = 18;
    // `parent` defaults to the root menu; nested submenus (Copy/Download Image, inside
    // Image/Layout) pass their own immediate parent so the dust grows from the right row.
    auto subMenuIn = [&](QMenu& parent, const char* icon, const QString& title) -> StayOpenMenu* {
      auto* m = new StayOpenMenu(title, &parent);
      QAction* a = parent.addMenu(m);
      a->setIcon(themedIcon(QString::fromLatin1(icon), iconColor_, subIcon));
      support::revealSubmenu(*m, parent, *a);  // the same particle dust the top menu plays
      return m;
    };
    auto subMenu = [&](const char* icon, const QString& title) -> StayOpenMenu* {
      return subMenuIn(menu, icon, title);
    };
    // A submenu-opener row still wants to SHOW a hint shortcut (it can't carry a real
    // QAction::shortcut() of its own — that's what actually TRIGGERS the row it opens),
    // via the same "\t"-separated hint Qt renders real shortcuts with. Read the combo
    // off the action it hints at and format it NATIVELY (⌘/⌥/⇧, not literal "Ctrl+C"/
    // "Alt+B"): a hardcoded literal sits wider and un-glyphed beside the real ones, which
    // is what makes rows read as "the hotkey and arrow don't line up".
    auto hintTab = [](QAction* a) -> QString {
      if (!a || a->shortcut().isEmpty()) return QString();
      return QStringLiteral("\t") + a->shortcut().toString(QKeySequence::NativeText);
    };

    // Browser order: Fit FIRST (its most-reached-for entry), then Image/Layout,
    // then Fullscreen.
    menu.addAction(actFit_);

    // A row whose action isn't available right now is OMITTED from this menu, not
    // added-then-greyed: the menu is rebuilt fresh on every right-click (this whole
    // function), so skipping addAction() here is the desktop's version of the
    // browser's hide-not-disable (contextMenu.js syncState — same hasImg/hasLines the
    // enable-state below still computes, since the MENU BAR's copies of these actions
    // keep the conventional greyed-out behaviour; only this freshly-built menu omits).
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();

    // Image / Layout submenu (contextMenu.js:7-22). Copy Image / Download Image are
    // nested submenus of their own — actCopyImageSplit_/actSaveImageSplit_ ("With
    // Splitter") lead but stay invisible (QAction::setVisible, honored per-row by QMenu)
    // until a split compare view is active — syncContextActions()'s syncSplitCopyDownloadSlot()
    // call, just above this function's own body, already set that before this menu is
    // built. actCopyImage_/actSaveImage_ ("Current") are always there alongside it
    // rather than in its place. Neither opener row carries a hotkey hint of its
    // own (browser parity: contextMenu.js's ctx-copy-img/ctx-dl-img) — the row just opens
    // a flyout of variants, and the one combo lives on whichever variant is primary right
    // now, so repeating it on the opener doubled it up.
    QMenu* layout = subMenu("folder", "Image / Layout");
    layout->addAction(secImageAct_);
    // Their OWN compact chip instances, constructed before the root `hotkeyChips` so
    // its recursive wire() skips them ("already wired" marker). Declared out here so
    // they outlive the `if (hasImg)` blocks and stay alive for the whole menu.exec().
    std::optional<support::MenuHotkeyChips> copyImgChips, dlImgChips;
    if (hasImg) {
      QMenu* copyImg = subMenuIn(*layout, "copy", QStringLiteral("Copy Image"));
      populateExportVariantMenu(copyImg, /*copy=*/true);
      copyImgChips.emplace(copyImg, /*compact=*/true);
    }
    layout->addAction(actPasteImage_);
    if (hasImg) {
      QMenu* dlImg = subMenuIn(*layout, "download", "Download Image");
      populateExportVariantMenu(dlImg, /*copy=*/false);
      dlImgChips.emplace(dlImg, /*compact=*/true);
      layout->addAction(actShareImage_); // "Share Image"
    }
    layout->addSeparator();
    layout->addAction(secLayoutJsonAct_);
    if (hasLines) layout->addAction(actCopyLayout_);
    if (hasImg) layout->addAction(actPasteLayout_);
    if (hasLines) layout->addAction(actDownloadJson_);  // "Download Layout"
    layout->addAction(actUploadJson_);    // "Upload Layout"

    menu.addAction(actFullscreen_);
    menu.addSeparator();

    // Assistant submenu: a compact chat hosted in its own child menu, shown only
    // when a provider is configured (adds NO separator, so nothing dangles).
    // Live-input handling is scoped to THIS child menu — the root stays stock QMenu.
    if (settings_.llmProvider != QLatin1String("none")) {
      ensureChatMenuPanel();
      refreshLlmStatus();  // fresh provider dot/tooltip on the panel's gear
      StayOpenMenu* assistant = subMenu("sparkle", "Assistant");
      assistant->addAction(chatMenuAction_);
      assistant->setInteractiveArea(chatMenuPanel_, chatMenuInput_);
      // No separator BELOW it: the entry sits directly against the drawing
      // group (the separator above, after Fit, already opens the section). The
      // disabled case therefore leaves exactly the original separators.
    }

    // Drawing (contextMenu.js:28-31).
    menu.addAction(canvas_->isDrawing() ? actStopDraw_ : actStartDraw_);
    menu.addAction(actDrawLineNow_);
    menu.addAction(actDrawRectNow_);
    menu.addSeparator();

    // Toggles + clear (contextMenu.js:33-36).
    menu.addAction(actShowPoints_);
    menu.addAction(actShowLines_);
    if (hasLines) menu.addAction(actClearAll_);
    menu.addSeparator();

    // Style submenu (contextMenu.js:39-57).
    QMenu* style = subMenu("palette", "Style");
    style->addAction(pointSizeAction_);
    style->addAction(thicknessAction_);
    style->addAction(secLineStyleAct_);
    style->addAction(actStyleSolid_);
    style->addAction(actStyleDashed_);
    style->addAction(actStyleDotted_);

    // Image Filter submenu (contextMenu.js:59-74).
    // The \t column mirrors the browser's Alt+B badge on this parent row.
    QMenu* filter = subMenu("image", QStringLiteral("Image Filter") + hintTab(actCycleFilter_));
    filter->addAction(secFilterAct_);
    filter->addAction(actFilterNone_);
    filter->addAction(actFilterBW_);
    filter->addAction(actFilterSepia_);
    filter->addAction(actFilterInvert_);
    filter->addAction(actFilterContour_);
    filter->addAction(actFilterCustom_);
    filter->addSeparator();
    filter->addAction(tintColorAction_);

    // Transformation submenu (contextMenu.js:76-100): a "Coordinate Formulas" section with the
    // Allow Formulas checkbox and the x(x)/y(y) inputs (shown only while formulas are enabled).
    QMenu* transform = subMenu("function", "Transformation");
    transform->addAction(secCoordFormulasAct_);
    transform->addAction(ctxAllowFormulasAct_);
    transform->addAction(ctxFormulaXAct_);
    transform->addAction(ctxFormulaYAct_);

    // Tooltip submenu (contextMenu.js:96-107): enable toggle + the 3 row toggles, all
    // hosted QCheckBoxes so toggling one keeps the menu open (browser-parity live inputs).
    QMenu* tt = subMenu("message", "Tooltip");
    tt->addAction(actTooltipEnable_);
    tt->addSeparator();   // browser: a plain rule separates the toggle from the row group below
    tt->addAction(secShowInTooltipAct_);
    tt->addAction(actTtPage_);
    tt->addAction(actTtScreen_);
    tt->addAction(actTtCoords_);

    // No Deselect row — the browser menu ends at Tooltip (Esc still deselects).

    // Declared AFTER menu: destroyed before it on return, which is what makes restoring
    // the shared actions' text safe (menuHotkeys.hpp's header comment explains why).
    support::MenuHotkeyChips hotkeyChips(&menu);  // bordered keycap chips (.ctx-hotkey)
    // Per-row icon hover motion (.ctx-icon) comes from the app-wide filter
    // (iconMotion.hpp's QMenu branch) — no per-menu wiring needed.
    support::MenuShimmer shimmer(&menu);          // per-row hover sweep (browser parity: .ctx-item)
    support::revealMenu(menu, globalPos);  // grow-from-the-cursor pop
    menu.exec(globalPos);
  }

}  // namespace stencil::gui

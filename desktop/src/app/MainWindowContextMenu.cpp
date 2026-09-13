#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "guiHelpers.hpp"
#include "MenuHotkeys.hpp"
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "iconSet.hpp"
#include "LinksDialog.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSession.hpp"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QWidgetAction>

// The canvas context menu tree (contextMenu.js parity).

namespace stencil::gui {

  void MainWindow::showContextMenu(const QPoint& globalPos) {
    // No image, no menu — the single gate for all three ways in (contextMenu.js `if (!app.image) return`).
    if (!canvas_ || !canvas_->hasImage()) return;
    syncContextActions();

    // Order mirrors contextMenu.js inner() (~5-108). StayOpenMenu keeps a hosted checkbox/radio click from closing it.
    StayOpenMenu menu(this);

    // Rebuilt per right-click, so the icons are applied in the current theme's colour.
    const int subIcon = 18;
    // Nested submenus pass their own immediate parent so the dust grows from the right row.
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
    // An opener row cannot carry a real shortcut (that would TRIGGER the row it opens); show the hint via the same "\t"
    // column Qt renders real shortcuts with, formatted natively (⌘/⌥/⇧) so it lines up with the rest.
    auto hintTab = [](QAction* a) -> QString {
      if (!a || a->shortcut().isEmpty()) return QString();
      return QStringLiteral("\t") + a->shortcut().toString(QKeySequence::NativeText);
    };

    // Browser order: Fit FIRST, then Image/Layout, then Fullscreen.
    menu.addAction(actFit_);

    // An unavailable action is OMITTED, not greyed — the menu is rebuilt per right-click, the desktop's version of
    // the browser's hide-not-disable (contextMenu.js syncState); the MENU BAR copies keep the greyed behaviour.
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();

    // contextMenu.js:7-22. The "With Splitter" variants lead but stay invisible until a split compare view is active
    // (syncSplitCopyDownloadSlot already ran). Opener rows carry no hotkey hint (browser parity).
    QMenu* layout = subMenu("folder", "Image / Layout");
    layout->addAction(secImageAct_);
    // Constructed before the root `hotkeyChips` so its recursive wire() skips them; declared out here to outlive the `if (hasImg)` blocks.
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

    // Assistant submenu only when a provider is configured (adds NO separator). Live-input handling is scoped to THIS child menu.
    if (settings_.llmProvider != QLatin1String("none")) {
      ensureChatMenuPanel();
      refreshLlmStatus();  // fresh provider dot/tooltip on the panel's gear
      StayOpenMenu* assistant = subMenu("sparkle", QStringLiteral("Assistant") + hintTab(actChat_));
      assistant->addAction(chatMenuAction_);
      assistant->setInteractiveArea(chatMenuPanel_, chatMenuInput_);
      // No separator BELOW it, so the disabled case leaves exactly the original separators.
    }
    menu.addAction(actScript_);   // browser: #ctx-script, right under the Assistant

    // contextMenu.js:28-31
    menu.addAction(canvas_->isDrawing() ? actStopDraw_ : actStartDraw_);
    menu.addAction(actDrawLineNow_);
    menu.addAction(actDrawRectNow_);
    menu.addSeparator();

    // contextMenu.js:33-36
    menu.addAction(actShowPoints_);
    menu.addAction(actShowLines_);
    if (hasLines) menu.addAction(actClearAll_);
    menu.addSeparator();

    // contextMenu.js:39-57
    QMenu* style = subMenu("palette", "Style");
    style->addAction(pointSizeAction_);
    style->addAction(thicknessAction_);
    style->addAction(secLineStyleAct_);
    style->addAction(actStyleSolid_);
    style->addAction(actStyleDashed_);
    style->addAction(actStyleDotted_);

    // contextMenu.js:59-74; the \t column mirrors the browser's Alt+B badge.
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

    // contextMenu.js:76-100
    QMenu* transform = subMenu("function", "Transformation");
    transform->addAction(secCoordFormulasAct_);
    transform->addAction(ctxAllowFormulasAct_);
    transform->addAction(ctxFormulaXAct_);
    transform->addAction(ctxFormulaYAct_);

    // contextMenu.js:96-107; hosted QCheckBoxes so toggling keeps the menu open.
    QMenu* tt = subMenu("message", "Tooltip");
    tt->addAction(actTooltipEnable_);
    tt->addSeparator();   // browser: a plain rule separates the toggle from the row group below
    tt->addAction(secShowInTooltipAct_);
    tt->addAction(actTtPage_);
    tt->addAction(actTtScreen_);
    tt->addAction(actTtCoords_);

    // No Deselect row (browser parity; Esc still deselects).

    // Declared AFTER menu: destroyed before it, which makes restoring the shared actions' text safe (MenuHotkeys.hpp).
    support::MenuHotkeyChips hotkeyChips(&menu);  // bordered keycap chips (.ctx-hotkey)
    support::MenuShimmer shimmer(&menu);          // per-row hover sweep (browser parity: .ctx-item)
    support::revealMenu(menu, globalPos);  // grow-from-the-cursor pop
    menu.exec(globalPos);
  }

}  // namespace stencil::gui

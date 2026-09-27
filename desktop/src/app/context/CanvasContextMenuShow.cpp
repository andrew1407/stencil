#include "MainWindow.hpp"
#include "CanvasContextMenu.hpp"
#include "ChatSessionController.hpp"
#include "StayOpenMenu.hpp"
#include "CanvasWidget.hpp"
#include "MenuHotkeys.hpp"
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "iconSet.hpp"

// The canvas context menu tree (contextMenu.js parity).

namespace stencil::gui {

  void CanvasContextMenu::showContextMenu(const QPoint& globalPos) {
    // No image, no menu — the single gate for all three ways in (contextMenu.js `if (!app.image) return`).
    if (!w.canvas || !w.canvas->hasImage()) return;
    w.ctxMenu.menuAt = globalPos;
    syncContextActions();

    // Order mirrors contextMenu.js inner() (~5-108). StayOpenMenu keeps a hosted checkbox/radio click from closing it.
    StayOpenMenu menu(&w);

    // Rebuilt per right-click, so the icons are applied in the current theme's colour.
    const int subIcon = 18;
    // Nested submenus pass their own immediate parent so the dust grows from the right row.
    auto subMenuIn = [&](QMenu& parent, const char* icon, const QString& title) -> StayOpenMenu* {
      auto* m = new StayOpenMenu(title, &parent);
      QAction* a = parent.addMenu(m);
      a->setIcon(themedIcon(QString::fromLatin1(icon), w.painted.iconColor, subIcon));
      // The same particle dust the top menu plays, on the same slower clock.
      support::revealSubmenu(*m, parent, *a, support::CONTEXT_MENU_DUST_MS);
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
    menu.addAction(w.acts.fit);

    // An unavailable action is OMITTED, not greyed — the menu is rebuilt per right-click, the desktop's version of
    // the browser's hide-not-disable (contextMenu.js syncState); the MENU BAR copies keep the greyed behaviour.
    const bool hasImg = w.canvas->hasImage();
    const bool hasLines = !w.canvas->allLines().empty();

    // contextMenu.js:7-22. The "With Splitter" variants lead but stay invisible until a split compare view is active
    // (syncSplitCopyDownloadSlot already ran). Opener rows carry no hotkey hint (browser parity).
    QMenu* layout = subMenu("folder", "Image / Layout");
    layout->addAction(w.ctxMenu.secImageAct);
    // Constructed before the root `hotkeyChips` so its recursive wire() skips them; declared out here to outlive the `if (hasImg)` blocks.
    std::optional<support::MenuHotkeyChips> copyImgChips, dlImgChips;
    if (hasImg) {
      QMenu* copyImg = subMenuIn(*layout, "copy", QStringLiteral("Copy Image"));
      w.parts.exportMenus.populateExportVariantMenu(copyImg, /*copy=*/true);
      copyImgChips.emplace(copyImg, /*compact=*/true);
    }
    layout->addAction(w.acts.pasteImage);
    if (hasImg) {
      QMenu* dlImg = subMenuIn(*layout, "download", "Download Image");
      w.parts.exportMenus.populateExportVariantMenu(dlImg, /*copy=*/false);
      dlImgChips.emplace(dlImg, /*compact=*/true);
      layout->addAction(w.acts.shareImage); // "Share Image"
    }
    layout->addSeparator();
    layout->addAction(w.ctxMenu.secLayoutJsonAct);
    if (hasLines) layout->addAction(w.acts.copyLayout);
    if (hasImg) layout->addAction(w.acts.pasteLayout);
    if (hasLines) layout->addAction(w.acts.downloadJson);  // "Download Layout"
    layout->addAction(w.acts.uploadJson);    // "Upload Layout"

    menu.addAction(w.acts.fullscreen);
    menu.addSeparator();

    // Assistant submenu only when a provider is configured (adds NO separator). Live-input handling is scoped to THIS child menu.
    if (w.settings.llmProvider != QLatin1String("none")) {
      w.chatSession->ensureChatMenuPanel();
      w.chatSession->refreshLlmStatus();  // fresh provider dot/tooltip on the panel's gear
      StayOpenMenu* assistant = subMenu("sparkle", QStringLiteral("Assistant") + hintTab(w.acts.chat));
      assistant->addAction(w.chatSession->chatMenuAction);
      assistant->setInteractiveArea(w.chatSession->chatMenuPanel, w.chatSession->chatMenuInput);
      // No separator BELOW it, so the disabled case leaves exactly the original separators.
    }
    // browser: #ctx-script, right under the Assistant — a caret onto the same editor the
    // script window holds, scoped the same way. The window itself stays on Alt+Shift+S.
    w.parts.scriptHost.ensureScriptMenuPanel();
    StayOpenMenu* script = subMenu("script", QStringLiteral("Stencil Script") + hintTab(w.acts.script));
    script->addAction(w.ctxMenu.scriptAction);
    script->setInteractiveArea(w.ctxMenu.scriptPanel, w.ctxMenu.scriptEditor);

    // contextMenu.js:28-31
    menu.addAction(w.canvas->getIsDrawing() ? w.acts.stopDraw : w.acts.startDraw);
    menu.addAction(w.ctxMenu.drawLineNow);
    menu.addAction(w.ctxMenu.drawRectNow);
    menu.addSeparator();

    // contextMenu.js:33-36
    menu.addAction(w.acts.showPoints);
    menu.addAction(w.acts.showLines);
    if (hasLines) menu.addAction(w.acts.clearAll);
    menu.addSeparator();

    // contextMenu.js:39-57
    QMenu* style = subMenu("palette", "Style");
    style->addAction(w.ctxMenu.pointSizeAction);
    style->addAction(w.ctxMenu.thicknessAction);
    style->addAction(w.ctxMenu.secLineStyleAct);
    style->addAction(w.ctxMenu.styleSolid);
    style->addAction(w.ctxMenu.styleDashed);
    style->addAction(w.ctxMenu.styleDotted);

    // contextMenu.js:59-74; the \t column mirrors the browser's Alt+B badge.
    QMenu* filter = subMenu("image", QStringLiteral("Image Filter") + hintTab(w.acts.cycleFilter));
    filter->addAction(w.ctxMenu.secFilterAct);
    filter->addAction(w.ctxMenu.filterNone);
    filter->addAction(w.ctxMenu.filterBW);
    filter->addAction(w.ctxMenu.filterSepia);
    filter->addAction(w.ctxMenu.filterInvert);
    filter->addAction(w.ctxMenu.filterContour);
    filter->addAction(w.ctxMenu.filterCustom);
    filter->addSeparator();
    filter->addAction(w.ctxMenu.tintColorAction);

    // contextMenu.js:76-100
    QMenu* transform = subMenu("function", "Transformation");
    transform->addAction(w.ctxMenu.secCoordFormulasAct);
    transform->addAction(w.ctxMenu.allowFormulasAct);
    transform->addAction(w.ctxMenu.formulaXAct);
    transform->addAction(w.ctxMenu.formulaYAct);

    // contextMenu.js:96-107; hosted QCheckBoxes so toggling keeps the menu open.
    QMenu* tt = subMenu("message", "Tooltip");
    tt->addAction(w.ctxMenu.tooltipEnable);
    tt->addSeparator();   // browser: a plain rule separates the toggle from the row group below
    tt->addAction(w.ctxMenu.secShowInTooltipAct);
    tt->addAction(w.ctxMenu.ttPage);
    tt->addAction(w.ctxMenu.ttScreen);
    tt->addAction(w.ctxMenu.ttCoords);

    // No Deselect row (browser parity; Esc still deselects).

    // Declared AFTER menu: destroyed before it, which makes restoring the shared actions' text safe (MenuHotkeys.hpp).
    support::MenuHotkeyChips hotkeyChips(&menu);  // bordered keycap chips (.ctx-hotkey)
    support::MenuShimmer shimmer(&menu);          // per-row hover sweep (browser parity: .ctx-item)
    support::revealMenu(menu, globalPos, support::CONTEXT_MENU_DUST_MS);  // grow-from-the-cursor pop
    // Coming back from the flyout's file dialog: land on the script row, flyout open, as if
    // the chain had never been dismissed. The keyboard path, so QMenu keeps its own state.
    if (w.ctxMenu.reopenScriptPending) {
      w.ctxMenu.reopenScriptPending = false;
      QAction* row = script->menuAction();
      QTimer::singleShot(0, &menu, [&menu, row] {
        menu.setActiveAction(row);
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(&menu, &right);
      });
    }
    menu.exec(globalPos);
  }

}  // namespace stencil::gui

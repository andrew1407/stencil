#pragma once
#include "ActionsBuilder.hpp"
#include "CanvasContextMenu.hpp"
#include "ChatAppliers.hpp"
#include "DockChrome.hpp"
#include "DocumentPersistence.hpp"
#include "EditorView.hpp"
#include "ExportMenus.hpp"
#include "HoverTip.hpp"
#include "MenuBuilder.hpp"
#include "PopoverGestures.hpp"
#include "ProjectFlows.hpp"
#include "ScriptHost.hpp"
#include "SettingsDialogs.hpp"
#include "SourceOpener.hpp"
#include "StyleControls.hpp"
#include "ThemePainter.hpp"
#include "ToolbarBuilder.hpp"
#include "WindowAssembly.hpp"
#include "WindowEvents.hpp"

namespace stencil::gui {

  class MainWindow;

  // The parts the window composes, one job each (see each header). A part holds the window by
  // reference and is built with it; builders and dispatchers wire the rest, parts never meet.
  struct WindowParts {
    explicit WindowParts(MainWindow& w)
        : toolbarBuilder(w), actionsBuilder(w), menuBuilder(w), canvasMenu(w), sourceOpener(w),
          dockChrome(w), view(w), theme(w), projects(w), chatAppliers(w),
          persistence(w), popoverGestures(w), hoverTip(w), dialogs(w), scriptHost(w),
          exportMenus(w), assembly(w), events(w), styleControls(w) {}

    ToolbarBuilder toolbarBuilder;
    ActionsBuilder actionsBuilder;
    MenuBuilder menuBuilder;
    CanvasContextMenu canvasMenu;
    SourceOpener sourceOpener;
    DockChrome dockChrome;
    EditorView view;
    ThemePainter theme;
    ProjectFlows projects;
    ChatAppliers chatAppliers;
    DocumentPersistence persistence;
    PopoverGestures popoverGestures;
    HoverTip hoverTip;
    SettingsDialogs dialogs;
    ScriptHost scriptHost;
    ExportMenus exportMenus;
    WindowAssembly assembly;
    WindowEvents events;
    StyleControls styleControls;
  };

}  // namespace stencil::gui

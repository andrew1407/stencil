#pragma once

class QAction;
class QMenu;

namespace stencil::gui {

  // The window's QActions — menus, toolbar buttons and hotkeys all trigger these. Built by
  // MainWindow::buildActions in creation order, which the menus and the toolbar iterate.
  struct WindowActions {
    QAction* open = nullptr;
    QAction* openAnother = nullptr;
    QAction* crop = nullptr;
    QAction* rotateLeft = nullptr;
    QAction* rotateRight = nullptr;
    QAction* flipImage = nullptr;
    QAction* cycleFilter = nullptr;
    QAction* cycleCompare = nullptr;
    QAction* startDraw = nullptr;
    QAction* stopDraw = nullptr;
    QAction* newLine = nullptr;
    QAction* undo = nullptr;
    QAction* redo = nullptr;
    QAction* deleteLast = nullptr;
    QAction* deleteLine = nullptr;
    QAction* deletePoint = nullptr;
    QAction* clearAll = nullptr;
    QAction* deselect = nullptr;
    QAction* zoomIn = nullptr;
    QAction* zoomOut = nullptr;
    QAction* fit = nullptr;
    QAction* showPoints = nullptr;
    QAction* showLines = nullptr;
    QAction* allowFormulas = nullptr;
    QAction* tooltip = nullptr;
    QAction* theme = nullptr;
    QAction* panel = nullptr;
    QAction* toolbars = nullptr;
    QAction* fullscreen = nullptr;
    QAction* accent = nullptr;
    QAction* settings = nullptr;
    QAction* projects = nullptr;
    QAction* connect = nullptr;
    QAction* links = nullptr;
    QAction* description = nullptr;
    QAction* keywords = nullptr;
    QAction* newProject = nullptr;
    QAction* saveProject = nullptr;
    QAction* clearProject = nullptr;
    QAction* renameProject = nullptr;
    QAction* projectColor = nullptr;
    QAction* projectColorClear = nullptr;
    QAction* saveSession = nullptr;
    QAction* info = nullptr;
    QAction* incognito = nullptr;
    QAction* shortcuts = nullptr;
    QAction* contextMenu = nullptr;
    QAction* openIn = nullptr;
    QAction* copyProject = nullptr;   // the Image section's "Make a copy" (its scopes pop from the button)
    QAction* chat = nullptr;
    QAction* assistantSettings = nullptr;
    QAction* quit = nullptr;

    // Browser toolbar.js Image/Layout buttons and the paste listener.
    QAction* downloadJson = nullptr;
    QAction* uploadJson = nullptr;
    QAction* script = nullptr;
    QAction* saveProjectFile = nullptr;
    QAction* openProjectFile = nullptr;
    QAction* deleteProjectFile = nullptr;
    QAction* stencilLiveSync = nullptr;
    QAction* copyLayout = nullptr;
    QAction* pasteLayout = nullptr;
    // Only ONE of a pair holds the real shortcut at a time — Qt disallows ambiguous shortcuts.
    QAction* saveImage = nullptr;
    QAction* saveImageSplit = nullptr;
    QAction* saveImageOriginal = nullptr;
    QAction* saveImageTint = nullptr;
    QAction* copyImage = nullptr;
    QAction* copyImageSplit = nullptr;
    QAction* copyImageOriginal = nullptr;
    QAction* copyImageTint = nullptr;
    QAction* saveImageCurrentRow = nullptr;
    QAction* copyImageCurrentRow = nullptr;
    QAction* shareImage = nullptr;
    QAction* pasteImage = nullptr;
    QMenu* copyImageOptionsMenu = nullptr;
    QMenu* saveImageOptionsMenu = nullptr;
  };

}  // namespace stencil::gui

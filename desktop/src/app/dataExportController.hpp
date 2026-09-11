#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
#include "fileStore.hpp"  // stencil::gui::Settings + fileStore::LayoutMeta

class QWidget;

namespace stencil::gui {

  class CanvasWidget;
  class Notifications;

  // DataExportController: layout/image export + clipboard IO
  // Extracted from MainWindow (the layout JSON download/upload/copy/paste + image save/copy
  // methods). A plain (non-QObject) helper matching the desktop's collaborator idiom: it takes
  // its dependencies in the constructor and holds NO back-pointer to MainWindow. MainWindow's
  // QActions invoke these; the controller reads the canvas + settings, reports through
  // Notifications, and pulls the project name + full layout meta through the two callbacks (which
  // stay on MainWindow). pasteImage() stays in MainWindow (it creates a project) and delegates
  // its JSON-text fallback to pasteLayout() here.
  class DataExportController {
  public:
    DataExportController(QWidget* parent, CanvasWidget* canvas, Notifications* notify,
                         const Settings* settings,
                         std::function<QString()> projectBaseName,
                         std::function<fileStore::LayoutMeta()> currentLayoutMeta);

    void downloadLayout();
    void uploadLayout();
    void copyLayout();
    void pasteLayout();
    // Confirm-replace + dimension-mismatch guard, then adopt the parsed layout. Shared by
    // uploadLayout + pasteLayout here and MainWindow's applyLayoutFromSource (--layout).
    void applyLayoutJson(const QJsonObject& obj);
    // variant: "current" (default — tint + lines/points) | "original" (no tint, no
    // lines/points) | "tint" (tint only, no lines/points) | "split" (needs a split
    // compare view — the composite, no divider/knob baked in either).
    void saveImageFile(const QString& variant = "current");
    // Same four variants as saveImageFile, same meaning each — deliberately explicit
    // (browser/desktop parity): "current" is always the plain edited
    // image, split compare view or not; picking "split" is the only way to copy that
    // composite instead, exactly mirroring how "download" needs its own explicit row.
    void copyImageToClipboard(const QString& variant = "current");
    // Native OS share sheet (browser/extension parity: exportService.js shareImage()).
    // Writes the annotated render to a session-lifetime temp file and hands it to
    // support::showShareSheet — see shareImage.hpp for what that shows per OS. Only
    // reachable where shareSheetAvailable(); elsewhere the action itself is hidden.
    // `anchor` is the Share BUTTON itself, not `parent_` (the whole window) — macOS's
    // picker positions itself relative to anchor's bounds, and a window-sized anchor
    // popped up off in a corner of the window instead of next to the button that was
    // clicked. The caller resolves it at trigger time (MainWindow::buttonForAction).
    void shareImage(QWidget* anchor);

  private:
    // True while a split compare view (vertical/horizontal) is actually showing.
    bool inSplitCompare() const;

    QWidget* parent_;
    CanvasWidget* canvas_;
    Notifications* notify_;
    const Settings* settings_;
    std::function<QString()> projectBaseName_;
    std::function<fileStore::LayoutMeta()> currentLayoutMeta_;
  };

}  // namespace stencil::gui

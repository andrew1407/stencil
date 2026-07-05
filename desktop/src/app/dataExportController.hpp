#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
#include "fileStore.hpp"  // stencil::gui::Settings + fileStore::LayoutMeta

class QWidget;

namespace stencil::gui {

  class CanvasWidget;
  class Notifications;

  // ── DataExportController: layout/image export + clipboard IO ────────────────
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
    void saveImageFile();
    void copyImageToClipboard();

  private:
    QWidget* parent_;
    CanvasWidget* canvas_;
    Notifications* notify_;
    const Settings* settings_;
    std::function<QString()> projectBaseName_;
    std::function<fileStore::LayoutMeta()> currentLayoutMeta_;
  };

}  // namespace stencil::gui

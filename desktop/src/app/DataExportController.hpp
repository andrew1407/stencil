#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
#include "fileStore.hpp"  // stencil::gui::Settings + fileStore::LayoutMeta

class QWidget;

namespace stencil::gui {

  class CanvasWidget;
  class Notifications;

  // Layout/image export + clipboard IO, extracted from MainWindow. A plain helper with NO back-pointer: the project name
  // and layout meta come through the two callbacks. pasteImage() stays in MainWindow (it creates a project).
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
    // Confirm-replace + dimension-mismatch guard; shared with MainWindow's applyLayoutFromSource (--layout).
    void applyLayoutJson(const QJsonObject& obj);
    // variant: "current" (tint + lines/points) | "original" | "tint" | "split" (needs a split compare view; no divider baked in).
    void saveImageFile(const QString& variant = "current");
    // Same four variants, same meaning (browser/desktop parity): "current" is always the plain edited image, compare or not.
    void copyImageToClipboard(const QString& variant = "current");
    // Native share sheet (browser exportService.js shareImage()) over a session-lifetime temp file. `anchor` is the Share BUTTON,
    // not the window: macOS positions the picker relative to the anchor's bounds.
    void shareImage(QWidget* anchor);

  private:
    // True while a split compare view is showing.
    bool inSplitCompare() const;

    QWidget* parent_;
    CanvasWidget* canvas_;
    Notifications* notify_;
    const Settings* settings_;
    std::function<QString()> projectBaseName_;
    std::function<fileStore::LayoutMeta()> currentLayoutMeta_;
  };

}  // namespace stencil::gui

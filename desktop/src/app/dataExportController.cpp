#include "dataExportController.hpp"
#include "canvasWidget.hpp"
#include "notifications.hpp"
#include "../support/iconSet.hpp"
#include <QByteArray>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>

namespace stencil::gui {

  DataExportController::DataExportController(QWidget* parent, CanvasWidget* canvas,
                                            Notifications* notify, const Settings* settings,
                                            std::function<QString()> projectBaseName,
                                            std::function<fileStore::LayoutMeta()> currentLayoutMeta)
      : parent_(parent), canvas_(canvas), notify_(notify), settings_(settings),
        projectBaseName_(std::move(projectBaseName)),
        currentLayoutMeta_(std::move(currentLayoutMeta)) {}

  void DataExportController::downloadLayout() {
    if (canvas_->allLines().empty()) {
      notify_->error("No lines to export");  // drawingApp.js:2073 alert
      return;
    }
    const QString suggested = projectBaseName_() + "-layout.json";
    const QString path = QFileDialog::getSaveFileName(
        parent_, "Export layout JSON", suggested, "JSON (*.json)");
    if (path.isEmpty()) return;
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
        settings_->imageFilter, settings_->filterColor);
    // Indented, matching the browser's JSON.stringify(data, null, 2).
    const QByteArray bytes =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      notify_->error("Could not write file");
      return;
    }
    f.write(bytes);
    f.close();
    notify_->success("Layout exported");
  }

  // Import a layout JSON file and adopt it (with the confirm/dimension guards in
  // applyLayoutJson). Mirrors browser uploadJSON (drawingApp.js ~2092-2130).
  void DataExportController::uploadLayout() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");  // drawingApp.js:2102
      return;
    }
    const QString path = QFileDialog::getOpenFileName(
        parent_, "Import layout JSON", QString(), "JSON (*.json)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify_->error("Could not read file");
      return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
      notify_->error("Error loading JSON: " + err.errorString());
      return;
    }
    applyLayoutJson(doc.object());
  }

  // Copy the layout JSON text to the clipboard. Guards "no layout" like the
  // browser (drawingApp.js copyLayoutToClipboard ~2181-2196).
  void DataExportController::copyLayout() {
    if (canvas_->allLines().empty()) {
      notify_->error("No layout to copy");  // drawingApp.js:2183
      return;
    }
    // Copy the FULL layout (Ctrl+Alt+C): lines + filter/tint + crop + rotation +
    // page meta, matching the server-save envelope so every applied edit travels.
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
        settings_->imageFilter, settings_->filterColor,
        canvas_->cropRect(), canvas_->rotationQuarters(),
        currentLayoutMeta_());
    const QByteArray txt =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QGuiApplication::clipboard()->setText(QString::fromUtf8(txt));
    notify_->success("Layout JSON copied");
  }

  // Parse clipboard text as a layout JSON object and adopt it. Mirrors the
  // text branch of the browser paste listener (drawingApp.js :582-591).
  void DataExportController::pasteLayout() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
      notify_->error("Clipboard has no layout JSON");
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject() || !doc.object().value("lines").isArray()) {
      notify_->error("Clipboard has no layout JSON");
      return;
    }
    applyLayoutJson(doc.object());
  }

  // Confirm-replace + dimension-mismatch guard, then adopt the parsed layout.
  // Shared by uploadLayout + pasteLayout, mirroring the browser's uploadJSON /
  // applyPastedLayout flow (drawingApp.js ~2101-2222): replace prompt only when
  // lines already exist, dimension prompt only on mismatch, then setLines +
  // history. setLines emits changed(), so the panel/buttons refresh.
  void DataExportController::applyLayoutJson(const QJsonObject& obj) {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    // Existing lines: offer to KEEP them and add the incoming ones on top instead of
    // forcing an all-or-nothing replace. Mirrors the browser's Combine/Replace/Cancel
    // prompt (exportService.js #applyValidatedLayout).
    bool combine = false;
    if (!canvas_->allLines().empty()) {
      QMessageBox box(parent_);
      box.setWindowTitle("Existing layout");
      box.setText("Add the imported JSON on top of the current layout, or replace it?");
      QPushButton* combineBtn = box.addButton("Combine", QMessageBox::AcceptRole);
      QPushButton* replaceBtn = box.addButton("Replace", QMessageBox::DestructiveRole);
      QAbstractButton* cancelBtn = box.addButton(QMessageBox::Cancel);
      // Same glyphs as the browser's dialog: stack the incoming lines on the existing
      // ones, or swap one layout for the other (browser exportService.js altIcon /
      // confirmIcon). Each button says what it does twice — in word and in picture.
      const QColor txt = box.palette().color(QPalette::WindowText);
      combineBtn->setIcon(themedIcon("layers", txt, 15));
      replaceBtn->setIcon(themedIcon("swap", txt, 15));
      if (cancelBtn) cancelBtn->setIcon(themedIcon("x", txt, 15));
      box.setDefaultButton(combineBtn);
      box.exec();
      if (box.clickedButton() == combineBtn) combine = true;
      else if (box.clickedButton() != replaceBtn) {
        notify_->info("Import canceled");  // drawingApp.js:2107 "Upload canceled"
        return;
      }
    }
    int w = 0, h = 0;
    core::Lines lines = fileStore::parseLayoutJson(obj, w, h);
    if (w != canvas_->imageWidth() || h != canvas_->imageHeight()) {
      if (QMessageBox::question(parent_, "Dimension mismatch",
                                "Image dimensions do not match. Continue anyway?")
          != QMessageBox::Yes) {
        notify_->info("Import canceled");  // drawingApp.js:2113 dimension guard
        return;
      }
    }
    if (combine) {
      core::Lines merged = canvas_->allLines();          // existing first…
      merged.insert(merged.end(), lines.begin(), lines.end());   // …new on top
      lines = std::move(merged);
    }
    canvas_->setLines(lines);  // emits changed() -> refresh panel + buttons
    notify_->success(combine ? "Layout loaded (combined)" : "Layout loaded");
  }

  // Render the canvas (image + filter + overlay) to a file. Extension drives the
  // encoder (jpg/png/webp/bmp; anything else -> png). Mirrors the browser
  // saveImage mime map (drawingApp.js :2062-2068) but writes to a chosen path.
  void DataExportController::saveImageFile() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");  // drawingApp.js:2037 "No image"
      return;
    }
    const QString suggested = projectBaseName_() + "-drawing." +
                              canvas_->imageExt();
    const QString path = QFileDialog::getSaveFileName(
        parent_, "Save image", suggested,
        "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (path.isEmpty()) return;
    // Map the chosen extension to a Qt encoder format; default png (matching the
    // browser's mimeMap fallback, drawingApp.js:2063-2064).
    const QString ext = QFileInfo(path).suffix().toLower();
    const char* fmt = "PNG";
    if (ext == "jpg" || ext == "jpeg") fmt = "JPG";
    else if (ext == "webp") fmt = "WEBP";
    else if (ext == "bmp") fmt = "BMP";
    else if (ext == "png") fmt = "PNG";
    // withOverlay=true: bake the points/lines onto the saved image.
    if (canvas_->renderToImage(true).save(path, fmt)) {
      notify_->success("Image saved");
    } else {
      notify_->error("Could not save image");
    }
  }

  // Copy the RENDERED image — filter plus the visible lines/points — to the
  // clipboard. Mirrors the browser's copyImageToClipboard, which routes through
  // renderExportCanvas so every image action ships the same annotated result
  // (the old no-overlay copy predated that and dropped the user's edits).
  void DataExportController::copyImageToClipboard() {
    if (!canvas_->hasImage()) {
      notify_->error("No image to copy");  // drawingApp.js:2134
      return;
    }
    QGuiApplication::clipboard()->setImage(canvas_->renderToImage(true));
    notify_->success("Image copied to clipboard");
  }

}  // namespace stencil::gui

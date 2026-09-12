#include "dataExportController.hpp"
#include "canvasWidget.hpp"
#include "notifications.hpp"
#include "../support/guiHelpers.hpp"  // showSaveDialog
#include "../support/modalChrome.hpp"  // confirmModalChoice — the browser-styled question
#include "../support/iconSet.hpp"
#include "../support/shareImage.hpp"
#include <QByteArray>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPushButton>
#include <QTemporaryDir>

namespace stencil::gui {

  DataExportController::DataExportController(QWidget* parent, CanvasWidget* canvas,
                                            Notifications* notify, const Settings* settings,
                                            std::function<QString()> projectBaseName,
                                            std::function<fileStore::LayoutMeta()> currentLayoutMeta)
      : parent_(parent), canvas_(canvas), notify_(notify), settings_(settings),
        projectBaseName_(std::move(projectBaseName)),
        currentLayoutMeta_(std::move(currentLayoutMeta)) {}

  bool DataExportController::inSplitCompare() const {
    return canvas_->isSplitCompare();
  }

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

  // Browser uploadJSON (drawingApp.js ~2092-2130).
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

  // Browser copyLayoutToClipboard (drawingApp.js ~2181-2196).
  void DataExportController::copyLayout() {
    if (canvas_->allLines().empty()) {
      notify_->error("No layout to copy");  // drawingApp.js:2183
      return;
    }
    // The FULL layout, matching the server-save envelope so every applied edit travels.
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

  // The text branch of the browser paste listener (drawingApp.js :582-591).
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

  // Browser uploadJSON / applyPastedLayout (drawingApp.js ~2101-2222): replace prompt only with existing lines, dimension prompt only on mismatch.
  void DataExportController::applyLayoutJson(const QJsonObject& obj) {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    // Existing lines: offer to KEEP them (browser Combine/Replace/Cancel, exportService.js #applyValidatedLayout).
    bool combine = false;
    if (!canvas_->allLines().empty()) {
      // Replace is the confirm, Combine the alt.
      ConfirmSpec spec;
      spec.title = "Existing layout";
      spec.message = "Add the imported JSON on top of the current layout, or replace it?";
      spec.confirmLabel = "Replace";
      spec.confirmIcon = QStringLiteral("swap");
      spec.altLabel = "Combine";
      spec.altIcon = QStringLiteral("layers");
      const ConfirmChoice pick = confirmModalChoice(parent_, spec);
      if (pick == ConfirmChoice::Alt) combine = true;
      else if (pick != ConfirmChoice::Confirm) {
        notify_->info("Import canceled");  // drawingApp.js:2107 "Upload canceled"
        return;
      }
    }
    int w = 0, h = 0;
    core::Lines lines = fileStore::parseLayoutJson(obj, w, h);
    if (w != canvas_->imageWidth() || h != canvas_->imageHeight()) {
      ConfirmSpec dim;
      dim.title = "Dimension mismatch";
      dim.message = "Image dimensions do not match. Continue anyway?";
      if (!confirmModal(parent_, dim)) {
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
}  // namespace stencil::gui


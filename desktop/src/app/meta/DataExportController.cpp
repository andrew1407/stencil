#include "DataExportController.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "../../support/guiHelpers.hpp"  // showSaveDialog
#include "../../support/modal/modalChrome.hpp"  // confirmModalChoice — the browser-styled question
#include "../../support/icon/iconSet.hpp"
#include "../../support/share/shareImage.hpp"
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
      : parent(parent), canvas(canvas), notify(notify), settings(settings),
        projectBaseName(std::move(projectBaseName)),
        currentLayoutMeta(std::move(currentLayoutMeta)) {}

  bool DataExportController::inSplitCompare() const {
    return canvas->isSplitCompare();
  }

  void DataExportController::downloadLayout() {
    if (canvas->allLines().empty()) {
      notify->error("No lines to export");  // drawingApp.js:2073 alert
      return;
    }
    const QString suggested = projectBaseName() + "-layout.json";
    const QString path = QFileDialog::getSaveFileName(
        parent, "Export layout JSON", suggested, "JSON (*.json)");
    if (path.isEmpty()) return;
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas->imageWidth(), canvas->imageHeight(), canvas->allLines(),
        settings->imageFilter, settings->filterColor);
    // Indented, matching the browser's JSON.stringify(data, null, 2).
    const QByteArray bytes =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      notify->error("Could not write file");
      return;
    }
    f.write(bytes);
    f.close();
    notify->success("Layout exported");
  }

  // Browser uploadJSON (drawingApp.js ~2092-2130).
  void DataExportController::uploadLayout() {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");  // drawingApp.js:2102
      return;
    }
    const QString path = QFileDialog::getOpenFileName(
        parent, "Import layout JSON", QString(), "JSON (*.json)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify->error("Could not read file");
      return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
      notify->error("Error loading JSON: " + err.errorString());
      return;
    }
    applyLayoutJson(doc.object());
  }

  // Browser copyLayoutToClipboard (drawingApp.js ~2181-2196).
  void DataExportController::copyLayout() {
    if (canvas->allLines().empty()) {
      notify->error("No layout to copy");  // drawingApp.js:2183
      return;
    }
    // The FULL layout, matching the server-save envelope so every applied edit travels.
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas->imageWidth(), canvas->imageHeight(), canvas->allLines(),
        settings->imageFilter, settings->filterColor,
        canvas->getCropRect(), canvas->getRotationQuarters(),
        currentLayoutMeta());
    const QByteArray txt =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QGuiApplication::clipboard()->setText(QString::fromUtf8(txt));
    notify->success("Layout JSON copied");
  }

  // The text branch of the browser paste listener (drawingApp.js :582-591).
  void DataExportController::pasteLayout() {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");
      return;
    }
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
      notify->error("Clipboard has no layout JSON");
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject() || !doc.object().value("lines").isArray()) {
      notify->error("Clipboard has no layout JSON");
      return;
    }
    applyLayoutJson(doc.object());
  }

  // Browser uploadJSON / applyPastedLayout (drawingApp.js ~2101-2222): replace prompt only with existing lines, dimension prompt only on mismatch.
  void DataExportController::applyLayoutJson(const QJsonObject& obj) {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");
      return;
    }
    // Existing lines: offer to KEEP them (browser Combine/Replace/Cancel, export/service.js #applyValidatedLayout).
    bool combine = false;
    if (!canvas->allLines().empty()) {
      // Replace is the confirm, Combine the alt.
      ConfirmSpec spec;
      spec.title = "Existing layout";
      spec.message = "Add the imported JSON on top of the current layout, or replace it?";
      spec.confirmLabel = "Replace";
      spec.confirmIcon = QStringLiteral("swap");
      spec.altLabel = "Combine";
      spec.altIcon = QStringLiteral("layers");
      const ConfirmChoice pick = confirmModalChoice(parent, spec);
      if (pick == ConfirmChoice::ALT) combine = true;
      else if (pick != ConfirmChoice::CONFIRM) {
        notify->info("Import canceled");  // drawingApp.js:2107 "Upload canceled"
        return;
      }
    }
    int w = 0, h = 0;
    core::Lines lines = fileStore::parseLayoutJson(obj, w, h);
    if (w != canvas->imageWidth() || h != canvas->imageHeight()) {
      ConfirmSpec dim;
      dim.title = "Dimension mismatch";
      dim.message = "Image dimensions do not match. Continue anyway?";
      if (!confirmModal(parent, dim)) {
        notify->info("Import canceled");  // drawingApp.js:2113 dimension guard
        return;
      }
    }
    if (combine) {
      core::Lines merged = canvas->allLines();          // existing first…
      merged.insert(merged.end(), lines.begin(), lines.end());   // …new on top
      lines = std::move(merged);
    }
    canvas->setLines(lines);  // emits changed() -> refresh panel + buttons
    notify->success(combine ? "Layout loaded (combined)" : "Layout loaded");
  }
}  // namespace stencil::gui


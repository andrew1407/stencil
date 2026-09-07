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
      // The browser's styled askAlt (exportService.js): Replace is the confirm
      // (swap one layout for the other), Combine the alt (stack the incoming lines
      // on the existing ones) — each button says what it does in word and picture.
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

  // Per-variant file-name suffix (browser exportService.js #VARIANT_META) and
  // copied-to-clipboard label. "current" (or anything unknown) takes the defaults.
  namespace {
    struct VariantMeta { const char* variant; const char* suffix; const char* copiedLabel; };
    constexpr VariantMeta kVariantMeta[] = {
        {"original", "-original", "Original image copied to clipboard"},
        {"tint", "-tint", "Tinted image copied to clipboard"},
        {"split", "-split", "Split image copied to clipboard"},
    };
    const VariantMeta* variantMeta(const QString& variant) {
      for (const auto& m : kVariantMeta)
        if (variant == QLatin1String(m.variant)) return &m;
      return nullptr;
    }
    QString variantSuffix(const QString& variant) {
      const VariantMeta* m = variantMeta(variant);
      return m ? QString::fromLatin1(m->suffix) : QString();
    }
  }

  // Render the canvas (per export variant) to a file. Extension drives the encoder
  // (jpg/png/webp/bmp; anything else -> png). Mirrors the browser saveImage mime map
  // (exportService.js) but writes to a chosen path.
  void DataExportController::saveImageFile(const QString& variant) {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");  // drawingApp.js:2037 "No image"
      return;
    }
    if (variant == "split" && !inSplitCompare()) {
      notify_->error("Turn on split compare to download with the splitter");
      return;
    }
    const QString suggested = projectBaseName_() + "-drawing" + variantSuffix(variant) + "." +
                              canvas_->imageExt();
    const QString path = showSaveDialog(parent_, "Save image", suggested,
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
    // "split" is always a CLEAN composite — the movable divider bar and its drag knob
    // are on-screen editor UI, not part of the picture (browser parity: exportService.js
    // renderSplitExportCanvas).
    if (canvas_->renderToImage(variant, /*withDivider=*/false).save(path, fmt)) {
      notify_->success("Image saved");
    } else {
      notify_->error("Could not save image");
    }
  }

  // Hand the annotated render to the OS's native share sheet (support/shareImage.hpp
  // — a different body per platform; browser/extension parity: exportService.js
  // shareImage(), same file name and title convention). The share UI needs an actual
  // FILE on disk, not raw bytes, so this writes one first — into a directory that
  // lives for the rest of the app's run (one static QTemporaryDir, not a fresh one
  // per share), since the native picker reads it asynchronously and may still be
  // open well after this call returns.
  void DataExportController::shareImage(QWidget* anchor) {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    static QTemporaryDir shareDir;
    if (!shareDir.isValid()) {
      notify_->error("Could not prepare a file to share");
      return;
    }
    const QString baseName = projectBaseName_();
    const QString path = shareDir.filePath(baseName + "-drawing.png");
    if (!canvas_->renderToImage(true).save(path, "PNG")) {
      notify_->error("Image encode failed");
      return;
    }
    // The BUTTON, not parent_ (the whole window) — see the header note.
    if (!support::showShareSheet(anchor ? anchor : parent_, path, baseName + " — Stencil"))
      notify_->error("Sharing not supported on this system");
  }

  // Copy the RENDERED image — per export variant — to the clipboard. Mirrors the
  // browser's copyImageToClipboard, which routes through renderExportCanvas so every
  // image action ships the same result. "current" is ALWAYS the plain edited image,
  // split compare view or not — "split" is its own explicit variant (like saveImageFile's),
  // the only way to copy the compare composite instead (no divider/knob baked in,
  // matching the download's own "with splitter" row).
  void DataExportController::copyImageToClipboard(const QString& variant) {
    if (!canvas_->hasImage()) {
      notify_->error("No image to copy");  // drawingApp.js:2134
      return;
    }
    if (variant == "split" && !inSplitCompare()) {
      notify_->error("Turn on split compare to copy with the splitter");
      return;
    }
    QGuiApplication::clipboard()->setImage(canvas_->renderToImage(variant));
    const VariantMeta* m = variantMeta(variant);
    notify_->success(m ? QString::fromLatin1(m->copiedLabel)
                       : QStringLiteral("Image copied to clipboard"));
  }

}  // namespace stencil::gui

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


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

  // Per-variant suffix and clipboard label (browser exportService.js #VARIANT_META).
  namespace {
    struct VariantMeta { const char* variant; const char* suffix; const char* copiedLabel; };
    constexpr VariantMeta VARIANT_META[] = {
        {"original", "-original", "Original image copied to clipboard"},
        {"tint", "-tint", "Tinted image copied to clipboard"},
        {"split", "-split", "Split image copied to clipboard"},
    };
    const VariantMeta* variantMeta(const QString& variant) {
      for (const auto& m : VARIANT_META)
        if (variant == QLatin1String(m.variant)) return &m;
      return nullptr;
    }
    QString variantSuffix(const QString& variant) {
      const VariantMeta* m = variantMeta(variant);
      return m ? QString::fromLatin1(m->suffix) : QString();
    }
  }

  // Extension drives the encoder (jpg/png/webp/bmp; else png) — browser saveImage mime map (exportService.js).
  void DataExportController::saveImageFile(const QString& variant) {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");  // drawingApp.js:2037 "No image"
      return;
    }
    if (variant == "split" && !inSplitCompare()) {
      notify->error("Turn on split compare to download with the splitter");
      return;
    }
    const QString suggested = projectBaseName() + "-drawing" + variantSuffix(variant) + "." +
                              canvas->imageExt();
    const QString path = showSaveDialog(parent, "Save image", suggested,
                                        "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (path.isEmpty()) return;
    // Default png (browser mimeMap fallback, drawingApp.js:2063-2064).
    const QString ext = QFileInfo(path).suffix().toLower();
    const char* fmt = "PNG";
    if (ext == "jpg" || ext == "jpeg") fmt = "JPG";
    else if (ext == "webp") fmt = "WEBP";
    else if (ext == "bmp") fmt = "BMP";
    else if (ext == "png") fmt = "PNG";
    // "split" is a CLEAN composite — no divider bar or knob (browser exportService.js renderSplitExportCanvas).
    if (canvas->renderToImage(variant, /*withDivider=*/false).save(path, fmt)) {
      notify->success("Image saved");
    } else {
      notify->error("Could not save image");
    }
  }

  // Native share sheet (browser exportService.js shareImage()). The picker needs a FILE and reads it asynchronously,
  // so it goes into one session-lifetime QTemporaryDir.
  void DataExportController::shareImage(QWidget* anchor) {
    if (!canvas->hasImage()) {
      notify->error("Load an image first");
      return;
    }
    static QTemporaryDir shareDir;
    if (!shareDir.isValid()) {
      notify->error("Could not prepare a file to share");
      return;
    }
    const QString baseName = projectBaseName();
    const QString path = shareDir.filePath(baseName + "-drawing.png");
    if (!canvas->renderToImage(true).save(path, "PNG")) {
      notify->error("Image encode failed");
      return;
    }
    // The BUTTON, not parent — see the header note.
    if (!support::showShareSheet(anchor ? anchor : parent, path, baseName + " — Stencil"))
      notify->error("Sharing not supported on this system");
  }

  // Browser copyImageToClipboard parity: "current" is ALWAYS the plain edited image; "split" is its own explicit variant.
  void DataExportController::copyImageToClipboard(const QString& variant) {
    if (!canvas->hasImage()) {
      notify->error("No image to copy");  // drawingApp.js:2134
      return;
    }
    if (variant == "split" && !inSplitCompare()) {
      notify->error("Turn on split compare to copy with the splitter");
      return;
    }
    QGuiApplication::clipboard()->setImage(canvas->renderToImage(variant));
    const VariantMeta* m = variantMeta(variant);
    notify->success(m ? QString::fromLatin1(m->copiedLabel)
                       : QStringLiteral("Image copied to clipboard"));
  }
}  // namespace stencil::gui


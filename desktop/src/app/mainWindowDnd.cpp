#include "mainWindow.hpp"
#include <QScrollArea>
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEasingCurve>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantAnimation>

// Photoshop-style drop-to-open, and the arrival motion a dropped picture plays.

namespace stencil::gui {

  namespace {
    // A droppable source resolved from a drag: a LOCAL file (keeps its path), a remote http(s)
    // URL (an image dragged from a browser page), or raw IMAGE bytes. Desktop can fetch remote
    // URLs freely (no browser CORS), so a cross-page image drag works here where the browser is
    // CORS-limited.
    struct DropSrc {
      enum Kind { None, LocalFile, Url, ImageData } kind = None;
      QString value;  // path (LocalFile) or url (Url); ImageData carries no string
    };
    DropSrc droppableSource(const QMimeData* m) {
      if (!m) return {};
      for (const QUrl& u : m->urls())
        if (u.isLocalFile()) return { DropSrc::LocalFile, u.toLocalFile() };
      for (const QUrl& u : m->urls()) {
        const QString s = u.toString();
        if (s.startsWith("http://") || s.startsWith("https://")) return { DropSrc::Url, s };
      }
      if (m->hasText()) {
        const QString t = m->text().trimmed();
        if (t.startsWith("http://") || t.startsWith("https://")) return { DropSrc::Url, t };
      }
      if (m->hasImage()) return { DropSrc::ImageData, QString() };
      return {};
    }
  }  // namespace

  void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Accept a dragged local file (image / video / layout JSON), a remote image URL, or raw
    // image bytes. Show the split LEFT-save / RIGHT-incognito overlay.
    if (droppableSource(event->mimeData()).kind == DropSrc::None) return;
    event->acceptProposedAction();
    if (dropZones_) {
      dropZones_->setActiveLeft(event->position().x() < width() / 2.0);
      dropZones_->showZones();
    }
  }

  void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (droppableSource(event->mimeData()).kind == DropSrc::None) return;
    event->acceptProposedAction();
    if (dropZones_) dropZones_->setActiveLeft(event->position().x() < width() / 2.0);
  }

  void MainWindow::dragLeaveEvent(QDragLeaveEvent*) {
    if (dropZones_) dropZones_->hideZones();
  }

  void MainWindow::dropEvent(QDropEvent* event) {
    if (dropZones_) dropZones_->hideZones();
    const DropSrc src = droppableSource(event->mimeData());
    if (src.kind == DropSrc::None) return;
    event->acceptProposedAction();

    // A local .json layout ignores the save/incognito split (it applies drawing data).
    if (src.kind == DropSrc::LocalFile &&
        QFileInfo(src.value).suffix().compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(src.value);
      return;
    }

    // RIGHT half = incognito, LEFT half = upload + save.
    const bool incognito = event->position().x() >= width() / 2.0;

    // Resolve a source string: local path, remote URL, or a data: URL for raw dropped pixels
    // (openImageSource decodes data: URLs), plus whether new-window is offerable.
    QString source = src.value;
    bool isLocal = false;
    if (src.kind == DropSrc::LocalFile) { isLocal = true; }
    else if (src.kind == DropSrc::ImageData) {
      const QImage img = qvariant_cast<QImage>(event->mimeData()->imageData());
      if (img.isNull()) return;
      source = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(pngBytes(img).toBase64());
    }

    // Open the dropped image via the LEFT (save) or RIGHT (incognito) path. Local files keep
    // their path (openImageHere/InNewWindow); URLs + raw pixels go through the async source path.
    const auto openHere = [&] { if (isLocal) openImageHere(source, incognito); else openSourceHere(source, -1, incognito); };
    const auto openNew = [&] { if (isLocal) openImageInNewWindow(source, incognito); else openSourceInNewWindow(source, -1, incognito); };

    // An image already open → ask this window vs a new one (the browser's askAlt:
    // two real answers plus a way out, in the same styled shell).
    if (canvas_->hasImage()) {
      ConfirmSpec spec;
      spec.title = tr("Open dropped image");
      spec.message = tr("An image is already open. Where should the dropped image open?");
      spec.confirmLabel = tr("This window");
      spec.confirmIcon = QStringLiteral("image");      // browser: confirmIcon 'image'
      spec.altLabel = tr("New window");
      spec.altIcon = QStringLiteral("external");       // …opened in another window
      const ConfirmChoice pick = confirmModalChoice(this, spec);
      if (pick == ConfirmChoice::Confirm) openHere();
      else if (pick == ConfirmChoice::Alt) openNew();
      return;
    }
    openHere();
  }

  // A fresh image ASSEMBLES from dust (Sweep::Gather; browser ghostIn). The
  // canvas waits at opacity 0, effect torn down at the end (an opacity effect
  // must never stay on a repainting canvas); snapshot BEFORE the effect goes on.
  void MainWindow::playImageArrival() {
    if (!canvas_) return;
    // Reduced motion: the image is simply THERE. Not just "no dust" — the opacity effect
    // has to go too, or the canvas sits blank for the whole flight and the arrival reads
    // as the image failing to load.
    if (support::motionReduced()) { canvas_->setGraphicsEffect(nullptr); return; }

    const bool dust = canvas_->hasImage() && scroll_ && scroll_->viewport()
        && !canvas_->visibleRegion().boundingRect().isEmpty()
        && DisintegrateOverlay::overRect(canvas_, canvas_->visibleRegion().boundingRect(),
                                         scroll_->viewport(), DisintegrateOverlay::Sweep::Gather);
    auto* fx = new QGraphicsOpacityEffect(canvas_);
    fx->setOpacity(0.0);
    canvas_->setGraphicsEffect(fx);
    // Only ever tear down OUR effect: two images arriving inside one flight (open, then
    // open again) would otherwise let the first timer reveal the second one's canvas.
    const QPointer<QGraphicsOpacityEffect> mine(fx);
    const auto done = [this, mine] {
      if (canvas_ && canvas_->graphicsEffect() == mine) canvas_->setGraphicsEffect(nullptr);
    };
    if (dust) {
      // Hidden for the whole flight, then simply revealed — the motes have already drawn
      // it into place, so fading it up as well would double the arrival.
      QTimer::singleShot(DisintegrateOverlay::kMs, canvas_, done);
      return;
    }
    // No dust to play (a canvas not on screen yet, or too small to tile): fall back to
    // the plain fade rather than to a hidden canvas.
    auto* anim = new QVariantAnimation(canvas_);
    anim->setDuration(360);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, canvas_,
            [fx](const QVariant& v) { fx->setOpacity(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, canvas_, done);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui

#include "MainWindow.hpp"
#include <QScrollArea>
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "launchOptions.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "ServerClient.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/ShimmerOverlay.hpp"

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
    // A LOCAL file, a remote http(s) URL, or raw IMAGE bytes; the desktop has no CORS limit on remote drags.
    struct DropSrc {
      enum Kind { NONE, LOCAL_FILE, URL, IMAGE_DATA } kind = NONE;
      QString value;  // path (LocalFile) or url (Url); ImageData carries no string
    };
    DropSrc droppableSource(const QMimeData* m) {
      if (!m) return {};
      for (const QUrl& u : m->urls())
        if (u.isLocalFile()) return { DropSrc::LOCAL_FILE, u.toLocalFile() };
      for (const QUrl& u : m->urls()) {
        const QString s = u.toString();
        if (s.startsWith("http://") || s.startsWith("https://")) return { DropSrc::URL, s };
      }
      if (m->hasText()) {
        const QString t = m->text().trimmed();
        if (t.startsWith("http://") || t.startsWith("https://")) return { DropSrc::URL, t };
      }
      if (m->hasImage()) return { DropSrc::IMAGE_DATA, QString() };
      return {};
    }
  }  // namespace

  void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Show the split LEFT-save / RIGHT-incognito overlay.
    if (droppableSource(event->mimeData()).kind == DropSrc::NONE) return;
    event->acceptProposedAction();
    if (dropZones_) {
      dropZones_->setActiveLeft(event->position().x() < width() / 2.0);
      dropZones_->showZones();
    }
  }

  void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (droppableSource(event->mimeData()).kind == DropSrc::NONE) return;
    event->acceptProposedAction();
    if (dropZones_) dropZones_->setActiveLeft(event->position().x() < width() / 2.0);
  }

  void MainWindow::dragLeaveEvent(QDragLeaveEvent*) {
    if (dropZones_) dropZones_->hideZones();
  }

  void MainWindow::dropEvent(QDropEvent* event) {
    if (dropZones_) dropZones_->hideZones();
    const DropSrc src = droppableSource(event->mimeData());
    if (src.kind == DropSrc::NONE) return;
    event->acceptProposedAction();

    // A .json layout ignores the save/incognito split.
    if (src.kind == DropSrc::LOCAL_FILE &&
        QFileInfo(src.value).suffix().compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(src.value);
      return;
    }

    const bool incognito = event->position().x() >= width() / 2.0;

    // Raw pixels become a data: URL (openImageSource decodes them).
    QString source = src.value;
    bool isLocal = false;
    if (src.kind == DropSrc::LOCAL_FILE) { isLocal = true; }
    else if (src.kind == DropSrc::IMAGE_DATA) {
      const QImage img = qvariant_cast<QImage>(event->mimeData()->imageData());
      if (img.isNull()) return;
      source = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(pngBytes(img).toBase64());
    }

    // Local files keep their path; URLs + raw pixels take the async source path.
    const auto openHere = [&] { if (isLocal) openImageHere(source, incognito); else openSourceHere(source, -1, incognito); };
    const auto openNew = [&] { if (isLocal) openImageInNewWindow(source, incognito); else openSourceInNewWindow(source, -1, incognito); };

    // An image already open → ask this window vs a new one (the browser's askAlt).
    if (canvas_->hasImage()) {
      ConfirmSpec spec;
      spec.title = tr("Open dropped image");
      spec.message = tr("An image is already open. Where should the dropped image open?");
      spec.confirmLabel = tr("This window");
      spec.confirmIcon = QStringLiteral("image");      // browser: confirmIcon 'image'
      spec.altLabel = tr("New window");
      spec.altIcon = QStringLiteral("external");       // …opened in another window
      const ConfirmChoice pick = confirmModalChoice(this, spec);
      if (pick == ConfirmChoice::CONFIRM) openHere();
      else if (pick == ConfirmChoice::ALT) openNew();
      return;
    }
    openHere();
  }

  // A fresh image ASSEMBLES from dust (browser ghostIn). An opacity effect must never stay on a repainting canvas; snapshot BEFORE it goes on.
  void MainWindow::playImageArrival() {
    if (!canvas_) return;
    // Reduced motion: the opacity effect has to go too, or the canvas sits blank for the whole flight.
    if (support::motionReduced()) { canvas_->setGraphicsEffect(nullptr); return; }

    const bool dust = canvas_->hasImage() && scroll_ && scroll_->viewport()
        && !canvas_->visibleRegion().boundingRect().isEmpty()
        && DisintegrateOverlay::overRect(canvas_, canvas_->visibleRegion().boundingRect(),
                                         scroll_->viewport(), DisintegrateOverlay::Sweep::GATHER);
    auto* fx = new QGraphicsOpacityEffect(canvas_);
    fx->setOpacity(0.0);
    canvas_->setGraphicsEffect(fx);
    // Only ever tear down OUR effect: a second arrival mid-flight has its own.
    const QPointer<QGraphicsOpacityEffect> mine(fx);
    const auto done = [this, mine] {
      if (canvas_ && canvas_->graphicsEffect() == mine) canvas_->setGraphicsEffect(nullptr);
    };
    if (dust) {
      // The motes already drew it into place; fading it up as well would double the arrival.
      QTimer::singleShot(DisintegrateOverlay::DUST_MS, canvas_, done);
      return;
    }
    // No dust to play: fall back to the plain fade, not to a hidden canvas.
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

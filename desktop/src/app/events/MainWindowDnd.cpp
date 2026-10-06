#include "MainWindow.hpp"
#include <QScrollArea>
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "DropZonesOverlay.hpp"
#include "dragPasteboard.hpp"
#include "dropSources.hpp"
#include "Notifications.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/modal/imageAnchor.hpp"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEasingCurve>
#include <QImage>
#include <QMimeData>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>

// Photoshop-style drop-to-open, and the arrival motion a dropped picture plays.

namespace stencil::gui {

  namespace {
    // The split the user AIMS at is the one painted, so the decision reads the overlay's own
    // rect — a window child, so its geometry is already in `pos`'s coordinates.
    bool onSaveHalf(const QWidget* win, const DropZonesOverlay* zones, const QPointF& pos) {
      const QRect r = zones ? zones->geometry() : win->rect();
      return pos.x() < r.x() + r.width() / 2.0;
    }

    // Encoding the bitmap a web drag rendered is the drop's cost alone, never a drag-move's.
    QString draggedBitmapUrl(const QMimeData* mime) {
      if (!mime || !mime->hasImage()) return {};
      const QImage img = qvariant_cast<QImage>(mime->imageData());
      if (img.isNull()) return {};
      return QStringLiteral("data:image/png;base64,")
          + QString::fromLatin1(pngBytes(img).toBase64());
    }

    // The browser's dropOverlay.js SINGLE table, word for word.
    void showKind(DropZonesOverlay* zones, DropKind kind, bool hasImage) {
      if (kind == DropKind::LAYOUT)
        zones->setSingle(QStringLiteral("layers"), QObject::tr("Apply layout"),
                         hasImage ? QObject::tr("Draw the .json layout over the open image")
                                  : QObject::tr("Open an image first — a layout needs one to draw on"));
      else if (kind == DropKind::SCRIPT)
        zones->setSingle(QStringLiteral("script"), QObject::tr("Run script"),
                         QObject::tr("Load the .stc script and run it"));
      else
        zones->setSingle();
    }

    enum class DropTarget { CANCEL, HERE, NEW_WINDOW };

    // An image already open → ask this window vs a new one (the browser's askAlt).
    DropTarget askDropTarget(QWidget* parent, bool hasImage) {
      if (!hasImage) return DropTarget::HERE;
      ConfirmSpec spec;
      spec.title = QObject::tr("Open dropped image");
      spec.message = QObject::tr("An image is already open. Where should the dropped image open?");
      spec.confirmLabel = QObject::tr("This window");
      spec.confirmIcon = QStringLiteral("image");      // browser: confirmIcon 'image'
      spec.altLabel = QObject::tr("New window");
      spec.altIcon = QStringLiteral("external");       // …opened in another window
      // A drop lands anywhere, so the question is not the drop point's to own.
      spec.flight = openImageConfirmFlight(parent);
      const ConfirmChoice pick = confirmModalChoice(parent, spec);
      if (pick == ConfirmChoice::CONFIRM) return DropTarget::HERE;
      return pick == ConfirmChoice::ALT ? DropTarget::NEW_WINDOW : DropTarget::CANCEL;
    }
  }  // namespace

  void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!canDrop(event->mimeData())) return;
    event->acceptProposedAction();
    if (overlays.dropZones) {
      showKind(overlays.dropZones, dropKindOf(event->mimeData()), canvas->hasImage());
      overlays.dropZones->showZones();   // fits the window first, so the read below is the painted split
      overlays.dropZones->setActiveLeft(onSaveHalf(this, overlays.dropZones, event->position()));
    }
  }

  void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (!canDrop(event->mimeData())) return;
    event->acceptProposedAction();
    if (overlays.dropZones) overlays.dropZones->setActiveLeft(onSaveHalf(this, overlays.dropZones, event->position()));
  }

  void MainWindow::dragLeaveEvent(QDragLeaveEvent*) {
    if (overlays.dropZones) overlays.dropZones->hideZones();
  }

  void MainWindow::dropEvent(QDropEvent* event) {
    const support::DragPasteboard native = support::readDragPasteboard();
    logDroppedMime(event->mimeData(), native);
    const QString bitmap = draggedBitmapUrl(event->mimeData());
    const DropSrc src = droppableSource(event->mimeData(), bitmap, native);
    const bool incognito = !onSaveHalf(this, overlays.dropZones, event->position());
    if (overlays.dropZones) overlays.dropZones->hideZones();
    if (src.kind == DropSrc::NONE) return;
    event->acceptProposedAction();

    // Nothing that crossed names a picture: the failure is the DRAG's, not an unreadable image's.
    if (isLinkOnlyDrag(event->mimeData(), bitmap, native)) {
      if (notify) notify->error(tr("That drag carried a link, not an image — nothing opened."));
      return;
    }

    switch (dropKindOf(src)) {
      case DropKind::LAYOUT: parts.sourceOpener.applyLayoutFromSource(src.value); return;
      case DropKind::SCRIPT: parts.scriptHost.runScriptFromFile(src.value); return;
      case DropKind::OPEN: break;
    }
    if (src.kind == DropSrc::LOCAL_FILE && src.value.endsWith(QLatin1String(".stencil"), Qt::CaseInsensitive)) {
      parts.sourceOpener.openProjectFileHere(src.value, incognito);
      return;
    }

    const QString source = src.value;
    const bool isLocal = src.kind == DropSrc::LOCAL_FILE;
    const QStringList fallbacks = src.fallbacks;
    const auto openHere = [&] { if (isLocal) parts.sourceOpener.openImageHere(source, incognito); else parts.sourceOpener.openSourceHere(source, -1, incognito, fallbacks); };
    const auto openNew = [&] { if (isLocal) parts.sourceOpener.openImageInNewWindow(source, incognito); else parts.sourceOpener.openSourceInNewWindow(source, -1, incognito, fallbacks); };

    const DropTarget where = askDropTarget(this, canvas->hasImage());
    if (where == DropTarget::CANCEL) return;
    if (where == DropTarget::NEW_WINDOW) openNew();
    else openHere();
  }

  // A fresh image ASSEMBLES from dust (browser ghostIn). An opacity effect must never stay on a repainting canvas; snapshot BEFORE it goes on.
  void MainWindow::playImageArrival() {
    if (!canvas) return;
    // Reduced motion: the opacity effect has to go too, or the canvas sits blank for the whole flight.
    if (support::motionReduced()) { canvas->setGraphicsEffect(nullptr); return; }

    const bool dust = canvas->hasImage() && scroll && scroll->viewport()
        && !canvas->visibleRegion().boundingRect().isEmpty()
        && DisintegrateOverlay::overRect(canvas, canvas->visibleRegion().boundingRect(),
                                         scroll->viewport(), DisintegrateOverlay::Sweep::GATHER,
                                         false, DisintegrateOverlay::DUST_MAX_CELLS,
                                         CANVAS_DUST_MS);
    QGraphicsOpacityEffect* fx = veilBehindDust(canvas);
    // Only ever tear down OUR effect: a second arrival mid-flight has its own.
    const QPointer<QGraphicsOpacityEffect> mine(fx);
    const auto done = [this, mine] {
      if (canvas && canvas->graphicsEffect() == mine) canvas->setGraphicsEffect(nullptr);
    };
    if (dust) {
      // The motes already drew it into place; fading it up as well would double the arrival.
      QTimer::singleShot(CANVAS_DUST_MS, canvas, done);
      return;
    }
    // No dust to play: fall back to the plain fade, not to a hidden canvas.
    auto* anim = new QVariantAnimation(canvas);
    anim->setDuration(360);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, canvas,
            [fx](const QVariant& v) { fx->setOpacity(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, canvas, done);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui

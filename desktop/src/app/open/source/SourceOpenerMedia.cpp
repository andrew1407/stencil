#include "MainWindow.hpp"
#include "SourceOpener.hpp"
#include "PlanAwait.hpp"
#include "fetchGuard.hpp"
#include "mainWindowHelpers.hpp"
#include "displayName.hpp"
#include "../../../support/localPath.hpp"
#include "CanvasWidget.hpp"
#include "imageAnchor.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "DataExportController.hpp"
#include "planExecutor.hpp"
#include "../../../support/modal/modalChrome.hpp"   // confirmModal — the browser-styled question

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QPointer>
#include <QFileInfo>
#include <functional>
#include <memory>

// Loading a source by path or URL, and the .stencil project file.

namespace stencil::gui {

  void SourceOpener::ensureMediaLoader() {
    if (w.mediaLoader) return;
    w.mediaLoader = new MediaLoader(&w);
    QObject::connect(w.mediaLoader, &MediaLoader::loaded, &w,
                     [this](const QImage& image, const QString& localPath) {
                       if (w.canvas->pictureGeneration() != w.docSource.mediaLoad) return;   // a newer load won
                       onLaunchImageLoaded(image, localPath);
                     });
    QObject::connect(w.mediaLoader, &MediaLoader::failed, &w, [this](const QString& msg) {
      w.docSource.pendingLaunchLayout.clear();
      w.docSource.pendingLaunchLayoutJson.clear();
      w.docSource.pendingProvSource.clear();
      w.docSource.pendingProvResource.clear();
      w.docSource.pendingServerTarget.clear();
      w.notify->error(msg);
    });
  }

  // A data: source (a browser hand-off, a dragged bitmap) is just another candidate here.
  void SourceOpener::openImageSource(const QString& src, int frame,
                                   const QStringList& fallbacks) {
    ensureMediaLoader();
    w.docSource.mediaLoad = w.canvas->beginPictureLoad();
    if (fallbacks.isEmpty()) w.mediaLoader->load(src, frame);
    else w.mediaLoader->loadFirstOf(QStringList{src} + fallbacks, frame);
  }

  // Retained raw bytes so a .stencil bundle embeds the untouched original; cleared on a read
  // failure.
  void SourceOpener::retainSourceFromFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    QByteArray bytes;
    if (!ext.isEmpty()) readFileBytes(path, bytes);
    w.docSource.setBytes(bytes, ext);   // empty bytes ⇒ buildStencilBytes re-encodes from pixels
  }

  // §10 openFile: dispatched by extension as /upload does in the console.
  void SourceOpener::chatOpenFileThen(const QString& raw, llm::OpDone done) {
    const QString path = support::expandHomePath(raw);
    if (!QFileInfo::exists(path))
      return done(false, QStringLiteral("openFile: %1 does not exist").arg(path));
    if (path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive)) {
      // Answered once the project is in, so the plan's next action edits it.
      PlanAwait::start(
          w, w.pop,
          [path, done](bool ok, const QString&) {
            done(ok, ok ? QString() : QStringLiteral("openFile: %1 could not be opened").arg(path));
          },
          [&](PlanAwait* await) {
            openProjectFile(path, [guard = QPointer<PlanAwait>(await)](bool ok) {
              if (guard) guard->settle(ok, QString());
            });
          });
      return;
    }
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
      if (!w.canvas->hasImage())
        return done(false, QStringLiteral("openFile: load a picture before applying a layout"));
      applyLayoutFromSource(path);
      return done(true, QString());
    }
    w.notify->info(QStringLiteral("Opening %1").arg(support::shortName(path)));
    chatLoadSourceThen(path, w.incognito, 0, [done](bool ok, const QString& why) {
      done(ok, ok ? QString() : QStringLiteral("openFile: %1").arg(why));
    });
  }

  // Answered once MediaLoader resolves, so the plan's next action edits the new picture; a load
  // that stalls lapses at the fetch bound and cannot hang the plan.
  void SourceOpener::chatLoadSourceThen(const QString& src, bool incognito, int frame,
                                        llm::OpDone done) {
    ensureMediaLoader();  // before OUR connects, so the canvas adopts first
    PlanAwait::start(
        w, w.pop, std::move(done),
        [&](PlanAwait* await) {
          QObject::connect(w.mediaLoader, &MediaLoader::loaded, await,
                           [await](const QImage&, const QString&) { await->settle(true, QString()); });
          QObject::connect(w.mediaLoader, &MediaLoader::failed, await,
                           [await](const QString& msg) { await->settle(false, msg); });
          openSourceHere(src, frame, incognito);
        },
        net::fetchGuard::fetchTimeoutMs(), false, QStringLiteral("timed out loading %1").arg(src));
  }

  // Ctrl+V: an image on the clipboard wins, else layout JSON text — drawingApp.js :563-591.
  void SourceOpener::pasteImage() {
    const QClipboard* clip = QGuiApplication::clipboard();
    const QImage img = clip->image();
    if (!img.isNull()) {
      if (w.canvas->hasImage()) {
        ConfirmSpec spec;
        spec.title = MainWindow::tr("Replace image");
        spec.message = MainWindow::tr("Replace current image with the pasted image?");
        spec.confirmLabel = MainWindow::tr("Replace");
        spec.confirmIcon = QStringLiteral("refresh");
        spec.flight = openImageConfirmFlight(&w);
        if (!confirmModal(&w, spec)) {
          w.notify->info("Image paste canceled");  // drawingApp.js:568
          return;
        }
      }
      w.activeProjectId.clear();  // pasted image is a fresh editor (a new project)
      w.canvas->loadFromImage(img);
      w.playImageArrival();
      w.docSource.setBytes({}, {});  // clipboard pixels have no encoded source → re-encode on bundle
      w.docSource.currentSource.clear();
      w.docSource.currentResource.clear();
      w.refreshActions();
      w.notify->success("Image pasted from clipboard");
      w.adoptCanvasAsLocalProject();
      return;
    }
    w.dataExport->pasteLayout();
  }
}  // namespace stencil::gui

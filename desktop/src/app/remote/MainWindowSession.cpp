#include "MainWindow.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ChatSessionController.hpp"
#include "theme.hpp"
#include "skinPrefs.hpp"

#include <QBuffer>

// What the window writes out — the session autosave, the bytes of the .stencil a project file holds
// and the layout envelope both carry — and the server connections it opens on demand.

namespace stencil::gui {

  void MainWindow::scheduleAutosave() {
    session.scheduleAutosave(settings.autosave, parts.persistence.sessionGates());
  }

  void MainWindow::saveSessionNow() {
    if (!SessionController::wantsSessionWrite(parts.persistence.sessionGates())) return;
    if (parts.persistence.sessionRestorePending()) return;
    Session s;
    s.imagePath = canvas->getImagePath();
    s.pageSize = pageSizeValue();
    s.scale = canvas->getScale();
    s.lines = canvas->allLines();
    s.customPageWidth = settings.customPageWidth;
    s.customPageHeight = settings.customPageHeight;
    // Filter / tint / draw mode ride along in the layout blob (browser storage.js:40-41,54).
    s.imageFilter = settings.imageFilter;
    s.filterColor = settings.filterColor;
    s.drawMode =
        canvas->getDrawMode() == CanvasWidget::DrawMode::RECT ? "rect" : "line";
    s.cropRect = canvas->getCropRect();
    s.rotationQuarters = canvas->getRotationQuarters();
    s.mirrored = canvas->getMirrored();
    s.activeProjectId = projectHost.activeProjectId;
    fileStore::saveSession(s);
  }

  stencil::net::ConnectionManager* MainWindow::ensureConnections() { return remote.session->ensureConnections(this); }

  void MainWindow::warnInsecureConnections() { remote.session->warnInsecureConnections(); }

  QByteArray MainWindow::buildStencilBytes() {
    if (!canvas->hasImage()) return {};
    const QImage& orig = canvas->getOriginalImage();
    QByteArray png;
    QString ext = QStringLiteral("png");
    if (!projectHost.docSource.sourceBytes.isEmpty()) {
      png = projectHost.docSource.sourceBytes;                                  // untouched original (lossless)
      if (!projectHost.docSource.sourceExt.isEmpty()) ext = projectHost.docSource.sourceExt;
    } else {
      QBuffer buf(&png);                                  // synthetic original — encode from pixels
      buf.open(QIODevice::WriteOnly);
      orig.save(&buf, "PNG");
    }
    fileStore::ProjectFileData pf;
    pf.name = projectHost.projectBaseName();
    pf.imageExt = ext;
    pf.imageBytes = png;
    pf.imageWidth = orig.width();
    pf.imageHeight = orig.height();
    pf.source = projectHost.docSource.currentSource;
    pf.resource = projectHost.docSource.currentResource;
    if (const Project* pr = projectHost.findProject(projectHost.activeProjectId.toStdString())) {
      pf.color = QString::fromStdString(pr->meta.color);
      for (const auto& k : pr->meta.keywords) pf.keywords << QString::fromStdString(k);
      pf.blank = pr->meta.blank;
      pf.blankColor = QString::fromStdString(pr->meta.blankColor);
    }
    pf.layout = fileStore::buildLayoutJson(
        canvas->imageWidth(), canvas->imageHeight(), canvas->allLines(),
        settings.imageFilter, settings.filterColor,
        canvas->getCropRect(), canvas->getRotationQuarters(), currentLayoutMeta(), canvas->getMirrored());
    pf.hasTheme = true;
    // "system" reads the shade last painted: resolving it again runs gdbus/gsettings on Linux.
    const bool painterFollowsMode = painted.done && !support::forcedDark();
    const bool dark = settings.themeMode == "system" && painterFollowsMode ? painted.dark
                                                                           : resolveDark(settings.themeMode);
    pf.themeMode = dark ? "dark" : "light";
    pf.themeAccent = settings.accentColor;
    // Persisted chat rides into the portable file only with the opt-in on (§12.3).
    if (settings.saveChatsWithProject && !projectHost.incognito) pf.chat = chatSession->buildActiveChatDoc();
    return fileStore::buildProjectFile(pf);
  }

  fileStore::LayoutMeta MainWindow::currentLayoutMeta() const {
    fileStore::LayoutMeta m;
    m.pageSize = settings.pageSize;
    m.customPageWidth = settings.customPageWidth;
    m.customPageHeight = settings.customPageHeight;
    m.allowFormulas = settings.allowFormulas;
    m.formulaX = settings.formulaX;
    m.formulaY = settings.formulaY;
    return m;
  }

}  // namespace stencil::gui

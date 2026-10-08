#include "MainWindow.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ChatSessionController.hpp"
#include "theme.hpp"

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
    s.activeProjectId = activeProjectId;
    fileStore::saveSession(s);
  }

  stencil::net::ConnectionManager* MainWindow::ensureConnections() {
    if (!remote.connections) {
      remote.connections = new stencil::net::ConnectionManager(this);
      // The desktop analogue of the browser connectionManager onChange → saveServers.
      connect(remote.connections, &stencil::net::ConnectionManager::changed, this, [this] {
        stencil::net::connectionStore::saveServers(remote.connections->snapshot());
      });
      // The manager starts null until this lazy creation.
      remote.session->setConnections(remote.connections);
    }
    return remote.connections;
  }

  void MainWindow::warnInsecureConnections() {
    if (!remote.connections) return;
    QStringList insecure;
    for (auto* c : remote.connections->getClients())
      if (stencil::net::ServerClient::isInsecureRemote(c->getBase())) insecure << c->getBase();
    if (insecure.isEmpty()) return;
    notify->error(
        QString("Insecure connection: %1 uses plaintext http — your access token and "
                "images are sent unencrypted. Use https on untrusted networks.")
            .arg(insecure.join(", ")));
  }

  QByteArray MainWindow::buildStencilBytes() {
    if (!canvas->hasImage()) return {};
    const QImage& orig = canvas->getOriginalImage();
    QByteArray png;
    QString ext = QStringLiteral("png");
    if (!docSource.sourceBytes.isEmpty()) {
      png = docSource.sourceBytes;                                  // untouched original (lossless)
      if (!docSource.sourceExt.isEmpty()) ext = docSource.sourceExt;
    } else {
      QBuffer buf(&png);                                  // synthetic original — encode from pixels
      buf.open(QIODevice::WriteOnly);
      orig.save(&buf, "PNG");
    }
    fileStore::ProjectFileData pf;
    pf.name = projectBaseName();
    pf.imageExt = ext;
    pf.imageBytes = png;
    pf.imageWidth = orig.width();
    pf.imageHeight = orig.height();
    pf.source = docSource.currentSource;
    pf.resource = docSource.currentResource;
    if (const Project* pr = findProject(activeProjectId.toStdString())) {
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
    pf.themeMode = resolveDark(settings.themeMode) ? "dark" : "light";
    pf.themeAccent = settings.accentColor;
    // Persisted chat rides into the portable file only with the opt-in on (§12.3).
    if (settings.saveChatsWithProject && !incognito) pf.chat = chatSession->buildActiveChatDoc();
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

#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <functional>
#include <optional>

class QWidget;

namespace stencil::net {
  class ServerClient;
}

namespace stencil::gui {

  // What removing `n` projects says, from the projects window or the assistant (browser
  // core/project/transferController.js clearedToast).
  inline QString clearedToast(int n) {
    return n == 1 ? QStringLiteral("Project cleared") : QStringLiteral("Projects cleared");
  }

  class MainWindow;
  class OpenInDialog;
  class ProjectsDialog;

  // The project flows behind the Projects window and the Project menu: open, create, rename and
  // erase a project, open a server project or a launch link, publish an incognito session, the
  // description / keywords windows, the project colour, and handing the session to another app.
  class ProjectFlows {
   public:
    explicit ProjectFlows(MainWindow& w) : w(w) {}

    void openProjects();
    void openProjectInNewWindow(const QString& id);
    void publishIncognitoToServer(const QString& serverUrl);
    bool openProjectByName(const QString& name);
    void openDescription();
    void openKeywords();
    void newProjectFromCanvas();
    void createProject(const QString& name);
    // Browser twin: remoteSync.js createRemoteProject.
    void createServerProject(const QString& serverUrl, const QString& name,
                             std::function<void()> onLinked = {});
    void clearCurrentProject();
    // Browser ui/projects/closeProject.js: asks over `over` (the window when null), grown from
    // `from`, unless `ask` is false, then closes the project this window holds; it stays in Projects.
    void closeActiveProject(QWidget* over = nullptr, const QRect& from = QRect(), bool ask = true);
    // The projects list's drag targets here: the window's zones, and Close for the open project.
    void wireProjectsDrag(ProjectsDialog& dlg, const std::function<bool()>& unsavedSession);
    // Its stay-open requests: the dialog confirms in place, the window acts and repaints it.
    void wireProjectsRequests(ProjectsDialog& dlg, const std::function<bool()>& unsavedSession);
    void resetToBlankEditor();
    void openServerProject(const QString& serverUrl, const QString& id, bool silent = false,
                           bool link = true);
    void openServerLaunch(const QString& serverUrl, const QString& id, bool incognito);
    // Browser openInModal.js, for the current session.
    void openInAnotherApp();
    void openInAnotherAppFor(const QString& id, const QString& serverUrl, const QRect& closeRect);
    struct OpenInSource {
      QString serverUrl, serverId;
      qint64 version = 0;
      QImage image;
      QString name, source, resource;
      QJsonObject layout;
      bool startIncognito = false;
    };
    void dispatchOpenIn(const OpenInSource& src, bool browserAvailable, bool telegramAvailable,
                        const std::function<int(OpenInDialog&)>& run);
    void adoptServerLayoutMeta(const QJsonObject& layout);
    void eraseLocalProject(const QString& id);

    // Browser modal.js.
    void openConnections();
    // Version-guarded PUT; a 409 surfaces "edited elsewhere" and leaves the link untouched.
    void saveToServer();

    QString activeProjectColor() const;
    QString currentProjectColor() const;
    QColor projectNameColor() const;   // its own colour, else the neutral grey the name is painted in
    void chooseProjectColor();
    void showProjectColorMenu();
    void setActiveProjectColor(const QString& color);
    void setActiveBlankColor();
    void applyBlankColor(const QColor& c);
    bool renameProjectById(const QString& id, const QString& name);
    // The Projects row menu's edits; `serverUrl` empty for a local row, whose `landed` never runs.
    using MetaLanded = std::function<void(bool ok, qint64 version)>;
    void setProjectDescriptionById(const QString& id, const QString& serverUrl, const QString& text,
                                   MetaLanded landed = {});
    void setProjectKeywordsById(const QString& id, const QString& serverUrl, const QStringList& keywords,
                                MetaLanded landed = {});
    void setProjectColorById(const QString& id, const QString& serverUrl, const QString& color,
                             std::function<void(bool ok)> done = {});
    std::optional<QString> normalizeProjectColor(const QString& color) const;

   private:
    using MetaPut = std::function<void(stencil::net::ServerClient*, qint64, std::function<void(bool, qint64, bool)>)>;
    // A local row's store edit, or a server row's PUT; `what` names the toasts.
    void setProjectMetaById(const QString& id, const QString& serverUrl, const QString& what, bool cleared,
                            const std::function<bool()>& applyLocal, MetaPut put, MetaLanded landed);
    // The server row's PUT, version-guarded with the meta retries; `saved` overrides "<what> saved".
    void putServerMeta(const QString& serverUrl, const QString& id, const QString& what, MetaPut put,
                       const QString& saved = {}, MetaLanded landed = {});
    MainWindow& w;
  };

}  // namespace stencil::gui

#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <functional>
#include <optional>

namespace stencil::gui {

  // What removing `n` projects says, from the projects window or the assistant (browser
  // core/project/transferController.js clearedToast).
  inline QString clearedToast(int n) {
    return n == 1 ? QStringLiteral("Project cleared") : QStringLiteral("Projects cleared");
  }

  class MainWindow;
  class OpenInDialog;

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
    void chooseProjectColor();
    void showProjectColorMenu();
    void setActiveProjectColor(const QString& color);
    void setActiveBlankColor();
    void applyBlankColor(const QColor& c);
    bool renameProjectById(const QString& id, const QString& name);
    void setProjectColorById(const QString& id, const QString& serverUrl, const QString& color,
                             std::function<void(bool ok)> done = {});
    std::optional<QString> normalizeProjectColor(const QString& color) const;

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui

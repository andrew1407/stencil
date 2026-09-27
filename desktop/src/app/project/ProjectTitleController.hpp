#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QColor;
class QScrollArea;
class QToolButton;
class QWidget;

namespace stencil::gui {

  class CanvasWidget;
  class Notifications;
  class ProjectNameBar;
  struct RemoteState;
  struct Settings;
  struct WindowActions;

  // The project's name as the window wears it, the browser's updateProjectTitle and its name field:
  // the title, the name row and its ✎/🎨/✓/✗ chips, the server frame, the actions a project enables
  // and the rename in place. The registry, the colours and the image readout stay on MainWindow as hooks.
  class ProjectTitleController : public QObject {
    Q_OBJECT
   public:
    // core::ProjectsStore's verdict on a local name.
    struct NameCheck {
      bool ok = true;
      QString reason;
    };

    struct Hooks {
      std::function<QString()> activeProjectName;
      std::function<NameCheck(const QString& name, const QString& exceptId)> checkName;
      // Validates and stores a local project's new name; false when it was refused.
      std::function<bool(const QString& id, const QString& name)> renameLocal;
      // The linked server record's colour, else the active local project's; '' = none.
      std::function<QString()> projectColor;
      // A blank page's fill; '' for a picture.
      std::function<QString()> blankColor;
      std::function<void(QToolButton* btn, const QColor& color)> paintSwatch;
      std::function<bool()> dark;
      std::function<void()> imageInfoChanged;
    };

    ProjectTitleController(QWidget* host, CanvasWidget* canvas, QScrollArea* scroll,
                           const bool& incognito, const QString& activeProjectId,
                           ProjectNameBar& nameBar, WindowActions& acts, RemoteState& remote,
                           const QPointer<Notifications>& notify, const Settings& settings,
                           Hooks hooks);

    void updateProjectTitle();
    void refreshProjectNameButtons();   // ✓/✗ visibility + ✓ enabled state, as the field is edited
    void updateNameHover();   // is the cursor over the name group (hover-reveal ✎/🎨)
    void applyProjectNameStyle(bool editing);
    void enterNameEdit();
    void commitProjectName();
    void cancelProjectName();

   private:
    void setPaintedOut(QWidget* widget, bool out);   // browser `visibility: hidden` — keeps the slot

    QWidget* host;
    CanvasWidget* canvas;
    QScrollArea* scroll;
    const bool& incognito;
    const QString& activeProjectId;
    ProjectNameBar& nameBar;
    WindowActions& acts;
    RemoteState& remote;
    // The window's: it dies with the scroll viewport while the window still stands.
    const QPointer<Notifications>& notify;
    const Settings& settings;
    Hooks h;
  };

}  // namespace stencil::gui

#pragma once
#include "fileStore.hpp"
#include <QDialog>
#include <QString>
#include <vector>

class QListWidget;

// Saved-projects browser. Mirrors browser/js/ui/projectsModal.js: list projects,
// open / delete one, or create a new one. exec() then read action()/selectedId()/
// newName() to apply the choice.
namespace stencil::gui {

  class ProjectsDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Action { None, Open, Delete, New };

    explicit ProjectsDialog(const std::vector<Project>& projects,
                            QWidget* parent = nullptr);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString newName() const { return newName_; }

   private:
    void refresh();
    void openSelected();
    void deleteSelected();
    void createNew();

    std::vector<Project> projects_;
    QListWidget* list_ = nullptr;
    Action action_ = Action::None;
    QString selectedId_;
    QString newName_;
  };

}

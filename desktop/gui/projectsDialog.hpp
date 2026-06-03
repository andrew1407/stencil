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
    enum class Action { None, Open, Delete, New, Renew };

    // `now` (epoch ms) is the reference point for the per-row expiry labels and
    // their warning/expired colouring; the caller passes its clock so the dialog
    // stays free of time sources.
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            QWidget* parent = nullptr);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString newName() const { return newName_; }

   private:
    void refresh();
    void openSelected();
    void deleteSelected();
    void renewSelected();
    void createNew();

    std::vector<Project> projects_;
    long long now_ = 0;
    QListWidget* list_ = nullptr;
    Action action_ = Action::None;
    QString selectedId_;
    QString newName_;
  };

}

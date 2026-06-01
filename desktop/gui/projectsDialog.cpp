#include "projectsDialog.hpp"
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace stencil::gui {

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects,
                                 QWidget* parent)
      : QDialog(parent), projects_(projects) {
    setWindowTitle("Projects");
    setMinimumSize(380, 320);

    // Most-recently-updated first, matching the browser store ordering.
    std::sort(projects_.begin(), projects_.end(),
              [](const Project& a, const Project& b) {
                return a.meta.updatedAt > b.meta.updatedAt;
              });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("<b>Saved projects</b>", this));

    list_ = new QListWidget(this);
    layout->addWidget(list_, 1);
    refresh();

    connect(list_, &QListWidget::itemDoubleClicked, this,
            &ProjectsDialog::openSelected);

    auto* row = new QHBoxLayout;
    auto* newBtn = new QPushButton("New Project", this);
    auto* openBtn = new QPushButton("Open", this);
    auto* delBtn = new QPushButton("Delete", this);
    auto* closeBtn = new QPushButton("Close", this);
    openBtn->setDefault(true);
    row->addWidget(newBtn);
    row->addStretch(1);
    row->addWidget(openBtn);
    row->addWidget(delBtn);
    row->addWidget(closeBtn);
    layout->addLayout(row);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(openBtn, &QPushButton::clicked, this, &ProjectsDialog::openSelected);
    connect(delBtn, &QPushButton::clicked, this, &ProjectsDialog::deleteSelected);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
  }

  void ProjectsDialog::refresh() {
    list_->clear();
    if (projects_.empty()) {
      auto* it = new QListWidgetItem("No projects yet", list_);
      it->setFlags(Qt::NoItemFlags);
      return;
    }
    for (const auto& pr : projects_) {
      std::size_t pts = 0;
      for (const auto& l : pr.lines) pts += l.points.size();
      auto* it = new QListWidgetItem(
          QString("%1   —   %2 line(s), %3 point(s)")
              .arg(QString::fromStdString(pr.meta.name))
              .arg(pr.lines.size())
              .arg(pts),
          list_);
      it->setData(Qt::UserRole, QString::fromStdString(pr.meta.id));
    }
    list_->setCurrentRow(0);
  }

  void ProjectsDialog::openSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::Open;
    accept();
  }

  void ProjectsDialog::deleteSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::Delete;
    accept();
  }

  void ProjectsDialog::createNew() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Project",
                                               "Project name:", QLineEdit::Normal,
                                               "Untitled", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    newName_ = name.trimmed();
    action_ = Action::New;
    accept();
  }

}

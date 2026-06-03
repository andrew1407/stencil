#include "projectsDialog.hpp"
#include "core/projectsStore.hpp"
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace stencil::gui {

  namespace {
    // Human expiry label for one project, mirroring the browser modal's
    // expiryLabel(): "EXPIRED", "expires in 1 day", or "expires in N days".
    QString expiryText(const core::ProjectsStore& store,
                       const core::ProjectMeta& meta, long long now) {
      if (store.isExpired(meta, now)) return "EXPIRED";
      const auto at = store.expiresAt(meta);
      if (!at.has_value()) return QString();
      const long long day = 24LL * 60 * 60 * 1000;
      long long days = (*at - now + day - 1) / day;  // ceil
      if (days < 0) days = 0;
      return days <= 1 ? QString("expires in 1 day")
                       : QString("expires in %1 days").arg(days);
    }
  }  // namespace

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects,
                                 long long now, QWidget* parent)
      : QDialog(parent), projects_(projects), now_(now) {
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
    auto* renewBtn = new QPushButton("🔄 Renew", this);
    renewBtn->setToolTip("Reset the 7-day expiry to start from now");
    auto* delBtn = new QPushButton("Delete", this);
    auto* closeBtn = new QPushButton("Close", this);
    openBtn->setDefault(true);
    row->addWidget(newBtn);
    row->addStretch(1);
    row->addWidget(openBtn);
    row->addWidget(renewBtn);
    row->addWidget(delBtn);
    row->addWidget(closeBtn);
    layout->addLayout(row);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(openBtn, &QPushButton::clicked, this, &ProjectsDialog::openSelected);
    connect(renewBtn, &QPushButton::clicked, this, &ProjectsDialog::renewSelected);
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
    const core::ProjectsStore store;  // pure helpers only; reads meta, no state
    for (const auto& pr : projects_) {
      std::size_t pts = 0;
      for (const auto& l : pr.lines) pts += l.points.size();
      const QString expiry = expiryText(store, pr.meta, now_);
      QString label = QString("%1   —   %2 line(s), %3 point(s)")
                          .arg(QString::fromStdString(pr.meta.name))
                          .arg(pr.lines.size())
                          .arg(pts);
      if (!expiry.isEmpty()) label += QString("   ·   %1").arg(expiry);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, QString::fromStdString(pr.meta.id));
      // Red once expired, amber within a day of expiry — mirrors the browser CSS.
      if (store.isExpired(pr.meta, now_))
        it->setForeground(QBrush(QColor("#dc3545")));
      else if (store.isExpiringSoon(pr.meta, now_))
        it->setForeground(QBrush(QColor("#e0a800")));
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

  void ProjectsDialog::renewSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::Renew;
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

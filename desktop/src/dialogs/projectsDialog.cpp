#include "projectsDialog.hpp"
#include "projectsStore.hpp"
#include "serverClient.hpp"
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <optional>

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

    // Modal name prompt with live validation (mirrors the browser's validated inline
    // rename): the ✓ (OK) button is enabled only when the trimmed name is non-empty,
    // ≤80 chars, and unique (excluding `exceptId`); its tooltip shows the reason when
    // disabled. Returns the accepted name, or nullopt on cancel.
    std::optional<QString> promptValidatedName(QWidget* parent, const QString& title,
                                               const QString& initial,
                                               const QString& exceptId,
                                               const std::vector<Project>& projects) {
      core::ProjectsStore store;
      std::vector<core::ProjectMeta> metas;
      for (const auto& p : projects) metas.push_back(p.meta);
      store.load(metas);

      QDialog d(parent);
      d.setWindowTitle(title);
      auto* lay = new QVBoxLayout(&d);
      lay->addWidget(new QLabel("Project name:", &d));
      auto* edit = new QLineEdit(initial, &d);
      edit->selectAll();
      lay->addWidget(edit);
      auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
      auto* okBtn = box->button(QDialogButtonBox::Ok);
      okBtn->setText(QString::fromUtf8("✓ Save"));
      box->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("✗ Cancel"));
      lay->addWidget(box);

      auto revalidate = [&]() {
        const auto res =
            store.validateName(edit->text().trimmed().toStdString(), exceptId.toStdString());
        okBtn->setEnabled(res.ok);
        okBtn->setToolTip(res.ok ? QStringLiteral("Save name")
                                 : QString::fromStdString(res.reason));
      };
      QObject::connect(edit, &QLineEdit::textChanged, &d, [&](const QString&) { revalidate(); });
      QObject::connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
      QObject::connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
      revalidate();
      if (d.exec() != QDialog::Accepted) return std::nullopt;
      return edit->text().trimmed();
    }
  }  // namespace

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects, long long now,
                                 stencil::net::ConnectionManager* connections,
                                 QWidget* parent)
      : QDialog(parent), projects_(projects), now_(now), connections_(connections) {
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
    auto* blankBtn = new QPushButton("🖼 Blank Image", this);
    blankBtn->setToolTip("Create a blank image (white, black, or any color) to draw on");
    auto* openBtn = new QPushButton("Open", this);
    auto* openNewWinBtn = new QPushButton("↗ New Window", this);
    openNewWinBtn->setToolTip("Open the selected project in a new window");
    auto* renameBtn = new QPushButton("✎ Rename", this);
    renameBtn->setToolTip("Rename the selected project");
    auto* renewBtn = new QPushButton("🔄 Renew", this);
    renewBtn->setToolTip("Reset the 7-day expiry to start from now");
    auto* delBtn = new QPushButton("Delete", this);
    auto* closeBtn = new QPushButton("Close", this);
    openBtn->setDefault(true);
    row->addWidget(newBtn);
    row->addWidget(blankBtn);
    row->addStretch(1);
    row->addWidget(openBtn);
    row->addWidget(openNewWinBtn);
    row->addWidget(renameBtn);
    row->addWidget(renewBtn);
    row->addWidget(delBtn);
    row->addWidget(closeBtn);
    layout->addLayout(row);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(blankBtn, &QPushButton::clicked, this, &ProjectsDialog::createBlank);
    connect(openBtn, &QPushButton::clicked, this, &ProjectsDialog::openSelected);
    connect(openNewWinBtn, &QPushButton::clicked, this,
            &ProjectsDialog::openSelectedInNewWindow);
    connect(renameBtn, &QPushButton::clicked, this, &ProjectsDialog::renameSelected);
    connect(renewBtn, &QPushButton::clicked, this, &ProjectsDialog::renewSelected);
    connect(delBtn, &QPushButton::clicked, this, &ProjectsDialog::deleteSelected);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Server (shared) projects: list them now and keep them live with a periodic
    // re-list while the dialog is open. The desktop talks REST only, so this
    // polling stands in for the browser modal's WebSocket project-event feed.
    if (connections_ && !connections_->urls().isEmpty()) {
      refreshRemote();
      remoteTimer_ = new QTimer(this);
      remoteTimer_->setInterval(5000);
      connect(remoteTimer_, &QTimer::timeout, this, &ProjectsDialog::refreshRemote);
      remoteTimer_->start();
    }
  }

  void ProjectsDialog::refreshRemote() {
    if (!connections_ || remoteBusy_) return;
    remoteBusy_ = true;
    remote_ = connections_->sharedProjects();  // synchronous REST (nested event loop)
    remoteBusy_ = false;
    refresh();
  }

  void ProjectsDialog::refresh() {
    // Preserve the selected row across a live remote re-list so the polling timer
    // doesn't yank the user's selection out from under them.
    const int prevRow = list_->currentRow();
    list_->clear();
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

    // Server-stored (shared) projects: a golden row + a server marker so they're
    // visually distinct from local ones (mirrors the browser's golden outline /
    // --remote-gold #d4a017). UserRole+1 carries the origin server URL; a non-empty
    // value marks the row as remote so Open routes to OpenRemote.
    const QColor gold("#d4a017");
    const QColor goldBand(212, 160, 23, 38);  // translucent gold fill (the "outline" band)
    for (const auto& sp : remote_) {
      QString label = QString::fromUtf8("🖧 %1   —   %2")
                          .arg(sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name)
                          .arg(sp.serverUrl);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, sp.id);
      it->setData(Qt::UserRole + 1, sp.serverUrl);
      it->setForeground(QBrush(gold));
      it->setBackground(QBrush(goldBand));  // gold band makes shared rows a distinct block
      QFont f = it->font();
      f.setBold(true);
      it->setFont(f);
      it->setToolTip(QString("Server project on %1").arg(sp.serverUrl));
    }

    if (list_->count() == 0) {
      auto* it = new QListWidgetItem("No projects yet", list_);
      it->setFlags(Qt::NoItemFlags);
      return;
    }
    list_->setCurrentRow(prevRow >= 0 && prevRow < list_->count() ? prevRow : 0);
  }

  void ProjectsDialog::openSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (!server.isEmpty()) {  // golden remote row → fetch + open from the server
      selectedServerUrl_ = server;
      action_ = Action::OpenRemote;
      accept();
      return;
    }
    action_ = Action::Open;
    accept();
  }

  void ProjectsDialog::openSelectedInNewWindow() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    // New-window / delete / rename / renew apply to LOCAL projects only.
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::OpenInNewWindow;
    accept();
  }

  void ProjectsDialog::deleteSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::Delete;
    accept();
  }

  void ProjectsDialog::renameSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    const QString id = it->data(Qt::UserRole).toString();
    const auto cur = std::find_if(projects_.begin(), projects_.end(),
                                  [&](const Project& p) {
                                    return QString::fromStdString(p.meta.id) == id;
                                  });
    const QString old = cur != projects_.end()
                            ? QString::fromStdString(cur->meta.name)
                            : QString();
    const auto name = promptValidatedName(this, "Rename Project", old, id, projects_);
    if (!name) return;
    selectedId_ = id;
    newName_ = *name;
    action_ = Action::Rename;
    accept();
  }

  void ProjectsDialog::renewSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::Renew;
    accept();
  }

  void ProjectsDialog::createBlank() {
    action_ = Action::NewBlank;
    accept();
  }

  void ProjectsDialog::createNew() {
    core::ProjectsStore store;
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projects_) metas.push_back(p.meta);
    store.load(metas);
    const QString seed = QString::fromStdString(store.defaultName());
    const auto name = promptValidatedName(this, "New Project", seed, QString(), projects_);
    if (!name) return;
    newName_ = *name;
    action_ = Action::New;
    accept();
  }

}

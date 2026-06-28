#include "projectsDialog.hpp"
#include "projectsStore.hpp"
#include "serverClient.hpp"
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
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

    // Paints a rounded golden outline around server (shared) rows — the desktop analogue
    // of the browser's `.project-remote` border. A row is remote when UserRole+1 is set.
    class RemoteOutlineDelegate : public QStyledItemDelegate {
     public:
      using QStyledItemDelegate::QStyledItemDelegate;
      void paint(QPainter* p, const QStyleOptionViewItem& opt,
                 const QModelIndex& idx) const override {
        QStyledItemDelegate::paint(p, opt, idx);
        if (idx.data(Qt::UserRole + 1).toString().isEmpty()) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(QColor("#d4a017"), 2));
        p->drawRoundedRect(QRectF(opt.rect).adjusted(1, 1, -1, -1), 6, 6);
        p->restore();
      }
    };

    // A square, center-cropped (cover) thumbnail for the uniform row icon — mirrors the
    // browser's `object-fit: cover` thumbnails so rows are equal height regardless of aspect.
    QPixmap squareThumb(const QPixmap& src, int size) {
      if (src.isNull()) return src;
      const QPixmap scaled =
          src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      const int x = (scaled.width() - size) / 2;
      const int y = (scaled.height() - size) / 2;
      return scaled.copy(x, y, size, size);
    }
  }  // namespace

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects, long long now,
                                 stencil::net::ConnectionManager* connections,
                                 const QHash<QString, QPixmap>& thumbs,
                                 QWidget* parent)
      : QDialog(parent), projects_(projects), now_(now), connections_(connections),
        thumbs_(thumbs) {
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
    // Row icons hold each project's edited-result preview (local) or its stored
    // result/original image (server); size the list's icon column to fit them.
    list_->setIconSize(QSize(56, 56));
    list_->setSpacing(6);  // vertical gaps so rows read as separate cards
    // Golden outline (not fill) around shared rows, mirroring the browser modal.
    list_->setItemDelegate(new RemoteOutlineDelegate(list_));
    // Hover-magnify: track moves over the viewport to pop a larger preview.
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
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
    // Move-between-storage buttons: local → server, and server → local. Shown only
    // when at least one server is connected; each acts on the matching row kind.
    auto* toServerBtn = new QPushButton(QString::fromUtf8("⇧ To server"), this);
    toServerBtn->setToolTip("Store the selected local project on a server, then remove the local copy");
    auto* toLocalBtn = new QPushButton(QString::fromUtf8("⇩ To local"), this);
    toLocalBtn->setToolTip("Copy the selected server project to local storage, then remove it from the server");
    auto* copyLocalBtn = new QPushButton(QString::fromUtf8("⧉ Local copy"), this);
    copyLocalBtn->setToolTip("Make a local copy (\"<name>-local\") of the selected server project and open it, leaving the server copy in place");
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    toServerBtn->setVisible(haveServers);
    toLocalBtn->setVisible(haveServers);
    copyLocalBtn->setVisible(haveServers);
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
    row->addWidget(toServerBtn);
    row->addWidget(toLocalBtn);
    row->addWidget(copyLocalBtn);
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
    connect(toServerBtn, &QPushButton::clicked, this, &ProjectsDialog::moveToServerSelected);
    connect(toLocalBtn, &QPushButton::clicked, this, &ProjectsDialog::moveToLocalSelected);
    connect(copyLocalBtn, &QPushButton::clicked, this, &ProjectsDialog::makeLocalCopySelected);
    connect(delBtn, &QPushButton::clicked, this, &ProjectsDialog::deleteSelected);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Server (shared) projects: list them now and keep them live with a periodic
    // re-list while the dialog is open. The desktop talks REST only, so this
    // polling stands in for the browser modal's WebSocket project-event feed.
    if (connections_ && !connections_->urls().isEmpty()) {
      // Defer the (synchronous) first listing to the next event-loop turn so the
      // dialog paints immediately with local rows + a "Loading shared projects…"
      // placeholder, instead of freezing on the network before it even shows.
      QTimer::singleShot(0, this, &ProjectsDialog::refreshRemote);
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
    remoteLoaded_ = true;  // first listing resolved → drop the loading placeholder
    refresh();
  }

  QPixmap ProjectsDialog::remoteThumb(const stencil::net::ServerProject& sp) {
    if (!connections_) return {};
    const QString key = QString("%1|%2|%3").arg(sp.serverUrl, sp.id).arg(sp.version);
    const auto cached = remoteThumbs_.constFind(key);
    if (cached != remoteThumbs_.constEnd()) return *cached;
    QPixmap pm;  // cache even a miss (empty) so a 404 isn't re-fetched every tick
    stencil::net::ServerClient* c = connections_->find(sp.serverUrl);
    if (c) {
      bool ok = false;
      QByteArray bytes = c->downloadFile(sp.id, "result", ok);
      if (!ok || bytes.isEmpty())
        bytes = c->downloadFile(sp.id, "original", ok);  // fall back to the original
      QImage img;
      if (ok && img.loadFromData(bytes))
        pm = QPixmap::fromImage(
            img.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (!pm.isNull()) {
      remoteThumbs_.insert(key, pm);
      return pm;
    }
    // No stored bytes — fetch the `source` URL in the background so the dialog never
    // blocks; the placeholder shows now and the icon swaps in on arrival (not cached yet).
    fetchSourceThumbAsync(key, sp);
    return {};
  }

  void ProjectsDialog::fetchSourceThumbAsync(const QString& key,
                                             const stencil::net::ServerProject& sp) {
    const QUrl u(sp.source);
    if (!u.isValid() || (u.scheme() != "http" && u.scheme() != "https")) {
      remoteThumbs_.insert(key, QPixmap());  // nothing to fetch — cache the miss
      return;
    }
    if (thumbInFlight_.contains(key)) return;  // already downloading this version
    thumbInFlight_.insert(key);
    if (!thumbNet_) thumbNet_ = new QNetworkAccessManager(this);
    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = thumbNet_->get(req);
    const QString id = sp.id;
    const QString serverUrl = sp.serverUrl;
    // `this` as context: Qt drops the connection (and never fires into a dead dialog)
    // if the dialog is destroyed before the download completes.
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, id, serverUrl] {
      thumbInFlight_.remove(key);
      QPixmap pm;
      if (reply->error() == QNetworkReply::NoError) {
        QImage img;
        if (img.loadFromData(reply->readAll()))
          pm = QPixmap::fromImage(
              img.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      }
      reply->deleteLater();
      remoteThumbs_.insert(key, pm);  // cache even a miss so we don't refetch
      if (pm.isNull()) return;
      // Swap the placeholder for the picture on the live row (found by id+server, so
      // a list rebuild between request and response can't target a stale item).
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (it->data(Qt::UserRole).toString() == id &&
            it->data(Qt::UserRole + 1).toString() == serverUrl) {
          it->setIcon(QIcon(squareThumb(pm, 112)));   // uniform square row icon (cover)
          it->setData(Qt::UserRole + 2, pm);          // full-aspect source for hover-magnify
          break;
        }
      }
    });
  }

  QPixmap ProjectsDialog::placeholderIcon(bool remote) const {
    QPixmap pm(56, 56);
    pm.fill(Qt::transparent);
    // A clean custom glyph for the 56×56 icon cell (so empty rows match thumbnail height),
    // hand-drawn to avoid QStyle's jarring blue SP_DriveNetIcon globe on macOS: server rows
    // get the gold "rack" glyph (matching the Servers dialog), local rows a muted picture glyph.
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (remote) {
      const QColor gold("#d4a017");
      p.setPen(QPen(gold, 3));
      p.setBrush(Qt::NoBrush);
      const QRectF top(14, 15, 28, 11);
      const QRectF bot(14, 30, 28, 11);
      p.drawRoundedRect(top, 3, 3);
      p.drawRoundedRect(bot, 3, 3);
      p.setPen(Qt::NoPen);
      p.setBrush(gold);
      p.drawEllipse(QPointF(20, top.center().y()), 2, 2);
      p.drawEllipse(QPointF(20, bot.center().y()), 2, 2);
    } else {
      const QColor muted = palette().color(QPalette::Disabled, QPalette::Text);
      const QRectF frame(13, 15, 30, 26);
      p.setPen(QPen(muted, 2.5));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(frame, 4, 4);
      p.setClipRect(frame);  // keep the little scene inside the frame
      p.setPen(Qt::NoPen);
      p.setBrush(muted);
      p.drawEllipse(QPointF(22, 23), 3, 3);  // sun
      QPolygonF mountain;
      mountain << QPointF(16, 41) << QPointF(27, 29) << QPointF(34, 35)
               << QPointF(41, 27) << QPointF(44, 41);
      p.drawPolygon(mountain);
    }
    return pm;
  }

  bool ProjectsDialog::eventFilter(QObject* obj, QEvent* ev) {
    if (list_ && obj == list_->viewport()) {
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list_->itemAt(vpos);
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Magnify only while over the icon cell (the left edge of the row), so the
        // preview doesn't pop up across the whole row's text.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const QRect vr = list_->visualItemRect(it);
          const QRect iconCell(vr.left(), vr.top(),
                               list_->iconSize().width() + 8, vr.height());
          overIcon = iconCell.contains(vpos);
        }
        if (overIcon) {
          if (!hoverPreview_) {
            hoverPreview_ = new QLabel(this, Qt::ToolTip);
            hoverPreview_->setStyleSheet(
                "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                "border-radius:8px;padding:4px;}");
          }
          hoverPreview_->setPixmap(
              src.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
          hoverPreview_->adjustSize();
          // Down-right of the cursor, flipped/clamped to stay on-screen.
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          QScreen* s = QGuiApplication::screenAt(cur);
          const QRect scr = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
          const QSize sz = hoverPreview_->size();
          QPoint gp = cur + QPoint(18, 18);
          if (gp.x() + sz.width() > scr.right()) gp.setX(cur.x() - 18 - sz.width());
          if (gp.y() + sz.height() > scr.bottom()) gp.setY(scr.bottom() - sz.height());
          if (gp.y() < scr.top()) gp.setY(scr.top());
          hoverPreview_->move(gp);
          hoverPreview_->show();
        } else if (hoverPreview_) {
          hoverPreview_->hide();
        }
      } else if (ev->type() == QEvent::Leave && hoverPreview_) {
        hoverPreview_->hide();
      }
    }
    return QDialog::eventFilter(obj, ev);
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
      // Edited-result preview (filtered image + drawn lines), pre-rendered by the
      // caller through the same canvas/export path. Absent for pathless (in-memory)
      // sources, whose pixels aren't reloadable from disk — those fall back to a
      // uniform placeholder tile so every row keeps the same height.
      const auto thumb = thumbs_.constFind(QString::fromStdString(pr.meta.id));
      if (thumb != thumbs_.constEnd() && !thumb->isNull()) {
        it->setIcon(QIcon(squareThumb(*thumb, 112)));
        it->setData(Qt::UserRole + 2, *thumb);
      } else {
        it->setIcon(QIcon(placeholderIcon(false)));
      }
      // Red once expired, amber within a day of expiry — mirrors the browser CSS.
      if (store.isExpired(pr.meta, now_))
        it->setForeground(QBrush(QColor("#dc3545")));
      else if (store.isExpiringSoon(pr.meta, now_))
        it->setForeground(QBrush(QColor("#e0a800")));
    }

    // Server-stored (shared) projects: a golden outline (painted by the delegate) +
    // bold gold text so they're visually distinct from local ones (mirrors the
    // browser's golden outline / --remote-gold #d4a017). UserRole+1 carries the
    // origin server URL; a non-empty value marks the row as remote so Open routes to
    // OpenRemote (and tells the delegate to draw the outline).
    const QColor gold("#d4a017");
    for (const auto& sp : remote_) {
      QString label = QString("%1   —   %2")
                          .arg(sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name)
                          .arg(sp.serverUrl);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, sp.id);
      it->setData(Qt::UserRole + 1, sp.serverUrl);
      it->setForeground(QBrush(gold));
      QFont f = it->font();
      f.setBold(true);
      it->setFont(f);
      it->setToolTip(QString("Server project on %1").arg(sp.serverUrl));
      // Edited preview: the project's rendered `result` (falling back to the
      // `original` if it was never saved), mirroring the browser modal's
      // makeRemoteRow result-with-original-fallback. Cached by id+version so the
      // periodic re-list doesn't re-download an unchanged project.
      const QPixmap pm = remoteThumb(sp);
      if (pm.isNull()) {
        it->setIcon(QIcon(placeholderIcon(true)));
      } else {
        it->setIcon(QIcon(squareThumb(pm, 112)));
        it->setData(Qt::UserRole + 2, pm);
      }
    }

    // While the first server listing is still in flight, show a loading hint rather
    // than a misleading "No projects yet" — the dialog itself already opened (the
    // remote fetch is deferred); this row is replaced when the listing resolves.
    if (connections_ && !connections_->urls().isEmpty() && !remoteLoaded_) {
      auto* it = new QListWidgetItem(QStringLiteral("Loading shared projects…"), list_);
      it->setFlags(Qt::NoItemFlags);
      it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
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

  void ProjectsDialog::moveToServerSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (!connections_ || connections_->urls().isEmpty()) return;
    const QStringList urls = connections_->urls();
    QString target = urls.first();
    if (urls.size() > 1) {  // pick which server to store it on
      bool ok = false;
      target = QInputDialog::getItem(this, "Move to server",
                                     "Store this project on which server? The local copy "
                                     "will be removed.",
                                     urls, 0, false, &ok);
      if (!ok || target.isEmpty()) return;
    }
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = target;
    action_ = Action::MoveToServer;
    accept();
  }

  void ProjectsDialog::moveToLocalSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (server.isEmpty()) return;  // server (golden) rows only
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = server;
    action_ = Action::MoveToLocal;
    accept();
  }

  void ProjectsDialog::makeLocalCopySelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (server.isEmpty()) return;  // server (golden) rows only
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = server;
    action_ = Action::MakeLocalCopy;
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

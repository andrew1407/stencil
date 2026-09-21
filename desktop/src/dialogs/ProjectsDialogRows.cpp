// What refresh() writes onto one row. Every Qt::UserRole offset here is read back by the delegate
// and pinned by tests/ProjectsDialogRows.headless.cpp.
#include "ProjectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include <QColor>
#include <QIcon>
#include <QListWidget>
#include <QPixmap>
#include <QStringList>
namespace stencil::gui {

  QString ProjectsDialog::projectRowTooltip(int w, int h, const QString& description,
                                            const QString& origin) const {
    QStringList lines;
    if (w > 0 && h > 0)
      lines << QString("%1x%2 px · %3").arg(w).arg(h).arg(
          h >= w ? QStringLiteral("portrait") : QStringLiteral("landscape"));
    if (!description.isEmpty()) lines << QString("Description: %1").arg(description);
    if (!origin.isEmpty()) lines << origin;
    return lines.join('\n');
  }

  void ProjectsDialog::addLocalProjectRow(const Project& pr, const core::ProjectsStore& store) {
    // No line/point counts (browser projects list parity).
    const QString expiry = expiryText(store, pr.meta, now);
    QString label = QString::fromStdString(pr.meta.name);
    const QString created = createdText(pr.meta.createdAt);
    if (!created.isEmpty()) label += QString("   ·   %1").arg(created);
    if (!expiry.isEmpty()) label += QString("   ·   %1").arg(expiry);
    auto* it = new QListWidgetItem(label, list);
    it->setData(Qt::UserRole, QString::fromStdString(pr.meta.id));
    it->setData(Qt::UserRole + 3, QString::fromStdString(pr.meta.name));
    {
      QStringList metaBits;
      if (!created.isEmpty()) metaBits << created;
      if (!expiry.isEmpty()) metaBits << expiry;
      it->setData(META_ROLE, metaBits.join(QStringLiteral(" · ")));
    }
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(batch.checked.contains("|" + QString::fromStdString(pr.meta.id))
                          ? Qt::Checked : Qt::Unchecked);
    // Absent for pathless (in-memory) sources — a uniform placeholder tile keeps the row height.
    const auto thumb = thumbs.constFind(QString::fromStdString(pr.meta.id));
    if (thumb != thumbs.constEnd() && !thumb->isNull()) {
      it->setIcon(QIcon(squareThumb(*thumb, 112)));
      it->setData(Qt::UserRole + 2, *thumb);
    } else {
      it->setIcon(QIcon(placeholderIcon(false)));
    }
    // UserRole+4 — the delegate paints ONLY the name in it. Red once expired, amber within a day,
    // else the per-project colour, else the shared neutral grey (browser/CLI default).
    const QString pcol = QString::fromStdString(pr.meta.color);
    const QColor custom(pcol);
    QColor nameCol;
    if (store.isExpired(pr.meta, now)) nameCol = QColor("#dc3545");
    else if (store.isExpiringSoon(pr.meta, now)) nameCol = QColor("#e0a800");
    else if (!pcol.isEmpty() && custom.isValid()) nameCol = custom;
    else nameCol = QColor("#80868f");
    it->setData(Qt::UserRole + 4, nameCol);
    QStringList kw;
    for (const auto& k : pr.meta.keywords) kw << QString::fromStdString(k);
    it->setData(Qt::UserRole + 5, kw.join(' '));
    // UserRole+6: file-origin flag → the delegate's bronze outline + file glyph.
    it->setData(Qt::UserRole + 6, pr.meta.fromFile);
    // UserRole+8: the project open in THIS editor → the delegate's "(Current)" mark.
    if (!activeProjectId.isEmpty() && QString::fromStdString(pr.meta.id) == activeProjectId)
      it->setData(ACTIVE_ROLE, true);
    // A LOCAL row already carries the "computer" badge (the browser tip has no origin line); a .stencil
    // project keeps its note.
    it->setToolTip(projectRowTooltip(pr.meta.imageW, pr.meta.imageH,
                              QString::fromStdString(pr.meta.description),
                              pr.meta.fromFile ? QStringLiteral("Opened from a .stencil project file")
                                               : QString()));
  }

  // One SERVER row: UserRole+1 carries the origin server URL; non-empty marks the row remote.
  void ProjectsDialog::addServerProjectRow(const stencil::net::ServerProject& sp) {
    QString label = QString("%1   —   %2")
                        .arg(sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name)
                        .arg(sp.serverUrl);
    const QString spCreated = createdText(sp.createdAt);
    if (!spCreated.isEmpty()) label += QString("   ·   %1").arg(spCreated);
    const QString spExpires = expiresText(sp.expiresAt);
    if (!spExpires.isEmpty()) label += QString("   ·   %1").arg(spExpires);
    auto* it = new QListWidgetItem(label, list);
    it->setData(Qt::UserRole, sp.id);
    it->setData(Qt::UserRole + 1, sp.serverUrl);
    {
      QStringList metaBits;
      if (!spCreated.isEmpty()) metaBits << spCreated;
      if (!spExpires.isEmpty()) metaBits << spExpires;
      it->setData(META_ROLE, metaBits.join(QStringLiteral(" · ")));
    }
    it->setData(Qt::UserRole + 3, sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(batch.checked.contains(sp.serverUrl + "|" + sp.id) ? Qt::Checked : Qt::Unchecked);
    // UserRole+4 — the name only, so the "— <url>" suffix stays the default colour. The gold outline
    // marks server rows, so the grey matches local rows (browser default — not gold).
    const QColor custom(sp.color);
    it->setData(Qt::UserRole + 4,
                (!sp.color.isEmpty() && custom.isValid()) ? custom : QColor("#80868f"));
    it->setData(Qt::UserRole + 5, sp.keywords.join(' '));
    it->setToolTip(projectRowTooltip(sp.imageW, sp.imageH, sp.description,
                              QString("Server project on %1").arg(sp.serverUrl)));
    // `result`, falling back to `original` (browser makeRemoteRow); cached by id+version.
    const QPixmap pm = remoteThumb(sp);
    if (pm.isNull()) {
      it->setIcon(QIcon(placeholderIcon(true)));
    } else {
      it->setIcon(QIcon(squareThumb(pm, 112)));
      it->setData(Qt::UserRole + 2, pm);
    }
  }

}  // namespace stencil::gui

// What refresh() writes onto one row: the local and server row builders and the tooltip they
// share. Every Qt::UserRole offset here is read back by the delegate and by
// tests/projectsDialogRows.headless.cpp, which pins it.
#include "projectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "serverClient.hpp"
#include <QColor>
#include <QIcon>
#include <QListWidget>
#include <QPixmap>
#include <QStringList>
namespace stencil::gui {

  // Multi-line row tooltip: image size with its orientation, the description when set,
  // then the origin note. No drawn-line length — nobody hovers a row for it.
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

  // One LOCAL project row: edited-result thumb, expiry-aware name colour, checkbox.
  void ProjectsDialog::addLocalProjectRow(const Project& pr, const core::ProjectsStore& store) {
    // Name · created · expiry only — no line/point counts (mirrors the browser projects list).
    const QString expiry = expiryText(store, pr.meta, now_);
    QString label = QString::fromStdString(pr.meta.name);
    const QString created = createdText(pr.meta.createdAt);
    if (!created.isEmpty()) label += QString("   ·   %1").arg(created);
    if (!expiry.isEmpty()) label += QString("   ·   %1").arg(expiry);
    auto* it = new QListWidgetItem(label, list_);
    it->setData(Qt::UserRole, QString::fromStdString(pr.meta.id));
    it->setData(Qt::UserRole + 3, QString::fromStdString(pr.meta.name));  // search key (name)
    // The stacked row's muted middle line + the accent its kebab chip paints with.
    {
      QStringList metaBits;
      if (!created.isEmpty()) metaBits << created;
      if (!expiry.isEmpty()) metaBits << expiry;
      it->setData(kMetaRole, metaBits.join(QStringLiteral(" · ")));
    }
    // Multi-select checkbox (key "|<id>" — empty server marks a local row).
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(checked_.contains("|" + QString::fromStdString(pr.meta.id))
                          ? Qt::Checked : Qt::Unchecked);
    // Edited-result preview, pre-rendered by the caller through the canvas/export
    // path. Absent for pathless (in-memory) sources — those fall back to a
    // uniform placeholder tile so every row keeps the same height.
    const auto thumb = thumbs_.constFind(QString::fromStdString(pr.meta.id));
    if (thumb != thumbs_.constEnd() && !thumb->isNull()) {
      it->setIcon(QIcon(squareThumb(*thumb, 112)));
      it->setData(Qt::UserRole + 2, *thumb);
    } else {
      it->setIcon(QIcon(placeholderIcon(false)));
    }
    // NAME colour (UserRole+4) — the delegate paints ONLY the name in it. Red once
    // expired, amber within a day of expiry (warnings win over the swatch), else
    // the per-project colour, else the shared neutral grey (browser/CLI default).
    const QString pcol = QString::fromStdString(pr.meta.color);
    const QColor custom(pcol);
    QColor nameCol;
    if (store.isExpired(pr.meta, now_)) nameCol = QColor("#dc3545");
    else if (store.isExpiringSoon(pr.meta, now_)) nameCol = QColor("#e0a800");
    else if (!pcol.isEmpty() && custom.isValid()) nameCol = custom;
    else nameCol = QColor("#80868f");
    it->setData(Qt::UserRole + 4, nameCol);
    // UserRole+5: space-joined keywords, the search key for the keyword/common modes.
    QStringList kw;
    for (const auto& k : pr.meta.keywords) kw << QString::fromStdString(k);
    it->setData(Qt::UserRole + 5, kw.join(' '));
    // UserRole+6: file-origin flag (opened from a .stencil) → the delegate's bronze outline
    // + file glyph. The tooltip names where the project lives (local disk vs a .stencil file).
    it->setData(Qt::UserRole + 6, pr.meta.fromFile);
    // UserRole+8: this row is the project open in THIS editor right now → the
    // delegate's "(Current)" mark, painted in the palette's live accent.
    if (!activeProjectId_.isEmpty() && QString::fromStdString(pr.meta.id) == activeProjectId_)
      it->setData(kActiveRole, true);
    // A LOCAL project says nothing about its origin here: the row already carries the
    // "computer" badge, and the browser's own tip carries no origin line at all. A .stencil
    // project keeps its note, which tells you more than that badge's one word does.
    it->setToolTip(projectRowTooltip(pr.meta.imageW, pr.meta.imageH,
                              QString::fromStdString(pr.meta.description),
                              pr.meta.fromFile ? QStringLiteral("Opened from a .stencil project file")
                                               : QString()));
  }

  // One SERVER (shared) project row: golden outline (delegate) + server badge. UserRole+1
  // carries the origin server URL, and a non-empty value is what marks the row remote.
  void ProjectsDialog::addServerProjectRow(const stencil::net::ServerProject& sp) {
    QString label = QString("%1   —   %2")
                        .arg(sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name)
                        .arg(sp.serverUrl);
    const QString spCreated = createdText(sp.createdAt);
    if (!spCreated.isEmpty()) label += QString("   ·   %1").arg(spCreated);
    const QString spExpires = expiresText(sp.expiresAt);
    if (!spExpires.isEmpty()) label += QString("   ·   %1").arg(spExpires);
    auto* it = new QListWidgetItem(label, list_);
    it->setData(Qt::UserRole, sp.id);
    it->setData(Qt::UserRole + 1, sp.serverUrl);
    {
      QStringList metaBits;
      if (!spCreated.isEmpty()) metaBits << spCreated;
      if (!spExpires.isEmpty()) metaBits << spExpires;
      it->setData(kMetaRole, metaBits.join(QStringLiteral(" · ")));
    }
    it->setData(Qt::UserRole + 3, sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name);  // search key
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(checked_.contains(sp.serverUrl + "|" + sp.id) ? Qt::Checked : Qt::Unchecked);
    // The NAME colour (UserRole+4) — the delegate paints ONLY the name in it, so the "— <url>"
    // suffix stays the default colour. Per-project colour when set, else the shared neutral grey
    // (same as local rows + the browser default — not gold). The gold outline marks server rows.
    const QColor custom(sp.color);
    it->setData(Qt::UserRole + 4,
                (!sp.color.isEmpty() && custom.isValid()) ? custom : QColor("#80868f"));
    it->setData(Qt::UserRole + 5, sp.keywords.join(' '));  // keyword search key
    it->setToolTip(projectRowTooltip(sp.imageW, sp.imageH, sp.description,
                              QString("Server project on %1").arg(sp.serverUrl)));
    // Edited preview: the rendered `result`, falling back to `original` (browser
    // makeRemoteRow parity). Cached by id+version so the periodic re-list
    // doesn't re-download an unchanged project.
    const QPixmap pm = remoteThumb(sp);
    if (pm.isNull()) {
      it->setIcon(QIcon(placeholderIcon(true)));
    } else {
      it->setIcon(QIcon(squareThumb(pm, 112)));
      it->setData(Qt::UserRole + 2, pm);
    }
  }

}  // namespace stencil::gui

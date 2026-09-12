// The Projects dialog's list rebuild: what survives a live re-list, the combined local+server
// sort order, and the placeholder rows. The per-row data is projectsDialogRows.cpp; both are
// pinned by tests/projectsDialogRows.headless.cpp.
#include "projectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "serverClient.hpp"
#include <QColor>
#include <QComboBox>
#include <QIcon>
#include <QListWidget>
#include <QPalette>
#include <QSet>
#include <limits>
#include "../support/filterFade.hpp"
namespace stencil::gui {

  // A row's identity across a rebuild: the server url + id it stands for, "temp" for the
  // pinned session row. Placeholders ("Loading…", "No projects yet") have none — they are
  // never counted as arrivals.
  static QString rebuildKeyOf(const QListWidgetItem* it) {
    if (!it) return QString();
    if (it->data(kTempRole).toBool()) return QStringLiteral("temp");
    const QVariant id = it->data(Qt::UserRole);
    if (id.isNull()) return QString();
    return it->data(Qt::UserRole + 1).toString() + "|" + id.toString();
  }

  void ProjectsDialog::refresh() {
    // Preserve the selected row across a live remote re-list so the polling timer
    // doesn't yank the user's selection out from under them.
    const int prevRow = list_->currentRow();
    // What this list holds RIGHT NOW: a row the rebuild ADDS arrives out of the filter's
    // sand at the end rather than simply being there next frame (browser motion.js
    // filterDust). The very first build dusts nothing — the dialog has its own flight.
    QSet<QString> keysBefore;
    for (int i = 0; i < list_->count(); ++i) {
      const QString k = rebuildKeyOf(list_->item(i));
      if (!k.isEmpty()) keysBefore.insert(k);
    }
    // Whether this dialog has EVER built its list — not whether the list has rows in it
    // right now. A removal takes its row out of the view when the scatter ends, so the
    // rebuild that answers it can find the list empty; reading that as "this is the
    // opening build" is what made the pinned row simply appear, with no arrival at all.
    const bool wasBuilt = built_;
    built_ = true;
    // Keep the "Show:" per-server entries in step if servers were connected/disconnected.
    if (filter_ && connections_ && connections_->urls() != knownServerUrls_)
      rebuildFilterOptions();
    building_ = true;   // ignore the itemChanged storm from setCheckState below
    hideHoverPreview();   // clear() is about to delete whatever hoverItem_ points to
    closeInlineRename();  // …and the row the inline editor floats over
    list_->clear();
    const core::ProjectsStore store;  // pure helpers only; reads meta, no state
    buildSortedRows(store);

    // While the first server listing is still in flight, show a loading hint rather
    // than a misleading "No projects yet" — the dialog itself already opened (the
    // remote fetch is deferred); this row is replaced when the listing resolves.
    if (connections_ && !connections_->urls().isEmpty() && !remoteLoaded_) {
      auto* it = new QListWidgetItem(QStringLiteral("Loading shared projects…"), list_);
      it->setFlags(Qt::NoItemFlags);
      it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    }

    // Drop selections whose project is GONE — removed here, or from another window: the
    // bar reads checked_.size(), so a dead key kept "1 selected" on screen over an empty
    // list. A LOCAL key is "|<id>" (UserRole+1, its server url, is empty);
    // a remote key is left alone — a listing that has not answered is not proof it is gone.
    if (!checked_.isEmpty()) {
      QSet<QString> liveIds;
      liveIds.reserve(projects_.size());
      for (const Project& p : projects_) liveIds.insert(QString::fromStdString(p.meta.id));
      for (auto it = checked_.begin(); it != checked_.end();) {
        if (it->startsWith(QLatin1Char('|')) && !liveIds.contains(it->mid(1)))
          it = checked_.erase(it);
        else
          ++it;
      }
    }

    if (list_->count() == 0) {
      auto* it = new QListWidgetItem("No projects yet", list_);
      it->setFlags(Qt::NoItemFlags);
      building_ = false;
      updateBatchBar();
      return;
    }
    list_->setCurrentRow(prevRow >= 0 && prevRow < list_->count() ? prevRow : 0);
    applyFilter();   // re-hide rows the current filter excludes (survives the live re-list)
    building_ = false;
    updateBatchBar();
    // …and every row this rebuild ADDED forms out of sand on the filter's shared budget
    // but the longer ARRIVAL clock (browser twin: materialize, not the filter animator).
    // Last, with the bookkeeping settled: dustRowIn writes a role per row under the same
    // beforeFrame/afterFrame guard the filter's own frames use.
    if (wasBuilt)
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        const QString k = rebuildKeyOf(it);
        if (k.isEmpty() || it->isHidden() || keysBefore.contains(k)) continue;
        if (auto* fade = filterFade()) fade->dustRowIn(it, window(), kRowArriveMs);
      }
  }

  // A combined, sortable entry list (local + server) ordered by the active sort mode, so the
  // name/date modes interleave local and server rows (browser twin: js/ui/projectSort.js).
  // The pinned unsaved-session row goes above all of them.
  void ProjectsDialog::buildSortedRows(const core::ProjectsStore& store) {
    struct Entry { bool remote; int idx; QString key; QString name; long long date; };
    std::vector<Entry> entries;
    for (int i = 0; i < static_cast<int>(projects_.size()); ++i) {
      const auto& m = projects_[i].meta;
      entries.push_back({ false, i, "|" + QString::fromStdString(m.id),
                          QString::fromStdString(m.name).toLower(), static_cast<long long>(m.updatedAt) });
    }
    for (int i = 0; i < remote_.size(); ++i) {
      const auto& sp = remote_[i];
      entries.push_back({ true, i, sp.serverUrl + "|" + sp.id, sp.name.toLower(), static_cast<long long>(sp.createdAt) });
    }
    const QString mode = g_projectsSortMode;
    QHash<QString, int> manualPos;
    if (mode == "manual")
      for (int i = 0; i < g_projectsManualOrder.size(); ++i) manualPos.insert(g_projectsManualOrder[i], i);
    auto cmpName = [](const Entry& a, const Entry& b) -> int {
      int c = QString::localeAwareCompare(a.name, b.name);
      if (c) return c;
      if (a.date != b.date) return a.date > b.date ? -1 : 1;  // newest first on a name tie
      return QString::compare(a.key, b.key);
    };
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
      if (mode == "local") { if (a.remote != b.remote) return !a.remote; return cmpName(a, b) < 0; }
      if (mode == "server") { if (a.remote != b.remote) return a.remote; return cmpName(a, b) < 0; }
      if (mode == "date-desc") { if (a.date != b.date) return a.date > b.date; return cmpName(a, b) < 0; }
      if (mode == "date-asc") { if (a.date != b.date) return a.date < b.date; return cmpName(a, b) < 0; }
      if (mode == "manual") {
        const int pa = manualPos.value(a.key, std::numeric_limits<int>::max());
        const int pb = manualPos.value(b.key, std::numeric_limits<int>::max());
        if (pa != pb) return pa < pb;
        return cmpName(a, b) < 0;
      }
      return cmpName(a, b) < 0;  // name (default): server + local interleaved
    });
    // This window's own unsaved session, pinned above the sorted rows (browser parity).
    // Inert: no id, so no open, rename, checkbox, drag or "⋯".
    if (temporary_) {
      auto* it = new QListWidgetItem(incognito_ ? QStringLiteral("Incognito (unsaved)")
                                                : QStringLiteral("Temporary (unsaved)"), list_);
      it->setFlags(Qt::ItemIsEnabled);
      it->setData(kTempRole, true);
      it->setData(Qt::UserRole + 3, it->text());   // the search key, like every row's
      it->setData(kMetaRole, incognito_ ? QStringLiteral("Current window · incognito · never saved")
                                        : QStringLiteral("Current window · not saved to storage"));
      it->setData(Qt::UserRole + 4, QColor("#80868f"));   // the shared name grey, like every row
      it->setIcon(QIcon(temporaryIcon(incognito_)));
    }
    for (const auto& e : entries) {
      if (e.remote) addServerProjectRow(remote_[e.idx]);
      else addLocalProjectRow(projects_[e.idx], store);
    }
  }

}  // namespace stencil::gui

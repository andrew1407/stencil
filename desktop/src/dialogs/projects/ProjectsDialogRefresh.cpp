// The Projects dialog's list rebuild; the per-row data is ProjectsDialogRows.cpp; both are pinned by
// tests/ProjectsDialogRows.headless.cpp.
#include "ProjectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include <QColor>
#include <QComboBox>
#include <QIcon>
#include <QListWidget>
#include <QPalette>
#include <QSet>
#include <QTimer>
#include <limits>
#include "../../support/theme/filterFade.hpp"
namespace stencil::gui {

  namespace {
    // Identity across a rebuild: server url + id, "temp" for the pinned row; placeholders have none.
    QString rebuildKeyOf(const QListWidgetItem* it) {
      if (!it) return QString();
      if (it->data(TEMP_ROLE).toBool()) return QStringLiteral("temp");
      const QVariant id = it->data(Qt::UserRole);
      if (id.isNull()) return QString();
      return it->data(Qt::UserRole + 1).toString() + "|" + id.toString();
    }
  }  // namespace

  void ProjectsDialog::refresh() {
    const int prevRow = list->currentRow();
    // A row the rebuild ADDS arrives out of the filter's sand (browser motion.js filterDust); the very
    // first build dusts nothing.
    QSet<QString> keysBefore;
    for (int i = 0; i < list->count(); ++i) {
      const QString k = rebuildKeyOf(list->item(i));
      if (!k.isEmpty()) keysBefore.insert(k);
    }
    // EVER built, not "has rows now": a removal empties the view when its scatter ends, and reading
    // that as the opening build made the pinned row appear with no arrival.
    const bool wasBuilt = built;
    built = true;
    if (filter && connections && connections->urls() != knownServerUrls)
      rebuildFilterOptions();
    building = true;
    hideHoverPreview();
    closeInlineRename();
    list->clear();
    const core::ProjectsStore store;
    buildSortedRows(store);

    // While the first server listing is in flight, a loading hint rather than a misleading "No projects yet".
    if (connections && !connections->urls().isEmpty() && !remoteLoaded) {
      auto* it = new QListWidgetItem(QStringLiteral("Loading shared projects…"), list);
      it->setFlags(Qt::NoItemFlags);
      it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    }

    // Drop LOCAL keys whose project is GONE (the bar reads batch.checked.size()); a remote key is left
    // alone — a listing that has not answered is not proof it is gone.
    if (!batch.checked.isEmpty()) {
      QSet<QString> liveIds;
      liveIds.reserve(projects.size());
      for (const Project& p : projects) liveIds.insert(QString::fromStdString(p.meta.id));
      for (auto it = batch.checked.begin(); it != batch.checked.end();) {
        if (it->startsWith(QLatin1Char('|')) && !liveIds.contains(it->mid(1)))
          it = batch.checked.erase(it);
        else
          ++it;
      }
    }

    if (list->count() == 0) {
      auto* it = new QListWidgetItem("No projects yet", list);
      it->setFlags(Qt::NoItemFlags);
      building = false;
      updateBatchBar();
      return;
    }
    list->setCurrentRow(prevRow >= 0 && prevRow < list->count() ? prevRow : 0);
    applyFilter();
    building = false;
    updateBatchBar();
    // Rows this rebuild ADDED form out of sand on the ARRIVAL clock, last and one beat later, so a
    // removal's ash has the screen to itself first (browser beginRemoval). Only while ON SCREEN.
    if (wasBuilt && isVisible()) {
      QSet<QString> arriving;
      for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* it = list->item(i);
        const QString k = rebuildKeyOf(it);
        if (k.isEmpty() || it->isHidden() || keysBefore.contains(k)) continue;
        arriving.insert(k);
      }
      if (!arriving.isEmpty())
        // Re-found by key: another rebuild may have replaced the items in the meantime.
        QTimer::singleShot(ROW_ARRIVE_DELAY_MS, this, [this, arriving] {
          for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* it = list->item(i);
            if (it->isHidden() || !arriving.contains(rebuildKeyOf(it))) continue;
            if (auto* fade = getFilterFade()) fade->dustRowIn(it, window(), ROW_ARRIVE_MS);
          }
        });
    }
  }

  // Local + server interleaved by the active sort mode (browser js/ui/projectSort.js); the pinned
  // unsaved-session row goes above all of them.
  void ProjectsDialog::buildSortedRows(const core::ProjectsStore& store) {
    struct Entry { bool remote; int idx; QString key; QString name; long long date; };
    std::vector<Entry> entries;
    for (int i = 0; i < static_cast<int>(projects.size()); ++i) {
      const auto& m = projects[i].meta;
      entries.push_back({ false, i, "|" + QString::fromStdString(m.id),
                          QString::fromStdString(m.name).toLower(), static_cast<long long>(m.updatedAt) });
    }
    for (int i = 0; i < remote.size(); ++i) {
      const auto& sp = remote[i];
      entries.push_back({ true, i, sp.serverUrl + "|" + sp.id, sp.name.toLower(), static_cast<long long>(sp.createdAt) });
    }
    const QString mode = g_projectsSortMode;
    QHash<QString, int> manualPos;
    if (mode == "manual")
      for (int i = 0; i < g_projectsManualOrder.size(); ++i) manualPos.insert(g_projectsManualOrder[i], i);
    auto cmpName = [](const Entry& a, const Entry& b) -> int {
      int c = QString::localeAwareCompare(a.name, b.name);
      if (c) return c;
      if (a.date != b.date) return a.date > b.date ? -1 : 1;
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
      return cmpName(a, b) < 0;
    });
    // Inert (browser parity): no id, so no open, rename, checkbox, drag or "⋯".
    if (temporary) {
      auto* it = new QListWidgetItem(incognito ? QStringLiteral("Incognito (unsaved)")
                                                : QStringLiteral("Temporary (unsaved)"), list);
      it->setFlags(Qt::ItemIsEnabled);
      it->setData(TEMP_ROLE, true);
      it->setData(Qt::UserRole + 3, it->text());
      it->setData(META_ROLE, incognito ? QStringLiteral("Current window · incognito · never saved")
                                        : QStringLiteral("Current window · not saved to storage"));
      it->setData(Qt::UserRole + 4, QColor("#80868f"));
      it->setIcon(QIcon(temporaryIcon(incognito)));
    }
    for (const auto& e : entries) {
      if (e.remote) addServerProjectRow(remote[e.idx]);
      else addLocalProjectRow(projects[e.idx], store);
    }
  }

}  // namespace stencil::gui

// The Projects dialog's list rebuild; the per-row data is ProjectsDialogRows.cpp; both are pinned by
// tests/dialogs/projects/list/ProjectsDialogRows.headless.cpp.
#include "ProjectsDialog.hpp"
#include "projectsRowChrome.hpp"
#include <algorithm>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QSet>
#include <QTimer>
#include "fileStore.hpp"
#include "projectSort.hpp"
#include "../../../support/theme/filterFade.hpp"
#include "../../../support/control/reveal/edgeFlight.hpp"
#include "../../../support/control/reveal/modeGhost.hpp"
#include "../../../support/motionPrefs.hpp"
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
    // "No projects yet"; `edge`, it comes in by the list's left edge `delay` ms on (browser renderList.js notice).
    void addEmptyRow(QListWidget* list, ListFilterFade* fade, QWidget* host, bool edge, int delay) {
      auto* it = new QListWidgetItem(QStringLiteral("No projects yet"), list);
      it->setFlags(Qt::NoItemFlags);
      if (edge && fade) fade->dustRowIn(it, host, ROW_ARRIVE_MS, /*fromEdge=*/true, delay);
    }
    // A row mid-retire outlives the rebuild its own removal triggers (the owner's setProjects), or the rows
    // below it jumped into its slot at once; the browser holds its render the same way (beginRemoval).
    QList<QPair<int, QListWidgetItem*>> takeDoomed(QListWidget* list) {
      QList<QPair<int, QListWidgetItem*>> out;
      for (int i = list->count() - 1; i >= 0; --i)
        if (list->item(i)->data(DOOMED_ROLE).toBool()) out.prepend({i, list->takeItem(i)});
      return out;
    }

    // Back at their old places; a pinned temporary row arriving where the removed first row stood takes that
    // slot (renderList.js inPlace), which `replaced` reports. Returns the count put back.
    int restoreDoomed(QListWidget* list, const QList<QPair<int, QListWidgetItem*>>& doomed, bool tempNew,
                      bool& replaced) {
      const bool tempArrives = tempNew && list->count() > 0 && list->item(0)->data(TEMP_ROLE).toBool();
      int carried = 0;
      for (const auto& [at, item] : doomed) {
        if (tempArrives && at == 0) { delete item; replaced = true; continue; }
        list->insertItem(std::min(at, list->count()), item);
        ++carried;
      }
      return carried;
    }
  }  // namespace

  void ProjectsDialog::refresh() {
    const int prevRow = list->currentRow();
    // A row the rebuild ADDS arrives out of the filter's sand (browser enterLeave.js filterDust); the very
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
    const auto doomed = takeDoomed(list);
    list->clear();
    const core::ProjectsStore store;
    buildSortedRows(store);
    bool replaced = false;
    const int carried = restoreDoomed(list, doomed, wasBuilt && !keysBefore.contains(QStringLiteral("temp")), replaced);

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

    const int edgeDelay = carried > 0 && !support::isDustAllowed() ? edgeFlightMs() : 0;   // arrivals trail the leave
    if (list->count() == carried) {
      addEmptyRow(list, getFilterFade(), window(), wasBuilt && isVisible() && modeGhostMotion(), edgeDelay);
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
      // Re-found by key: another rebuild may have replaced the items in the meantime.
      const auto arrive = [this, arriving, edgeDelay] {
        for (int i = 0; i < list->count(); ++i) {
          QListWidgetItem* it = list->item(i);
          if (it->isHidden() || !arriving.contains(rebuildKeyOf(it))) continue;
          if (auto* fade = getFilterFade()) fade->dustRowIn(it, window(), ROW_ARRIVE_MS, /*fromEdge=*/true, edgeDelay);
        }
      };
      // No ash to wait out (Sliding / Transition, or a held slot's fallen already): in now, never shown plain first.
      if (!arriving.isEmpty() && (!support::isDustAllowed() || replaced)) arrive();
      else if (!arriving.isEmpty()) QTimer::singleShot(support::motionMs(ROW_ARRIVE_DELAY_MS), this, arrive);
    }
  }

  void ProjectsDialog::patchServerRow(const QString& id, const QString& serverUrl, qint64 version,
                                      const std::function<void(stencil::net::ServerProject&)>& edit) {
    for (auto& sp : remote)
      if (sp.id == id && sp.serverUrl == serverUrl) {
        edit(sp);
        sp.version = version;
        refresh();
        return;
      }
  }

  // Local + server interleaved by the active sort mode (browser js/ui/projects/window/projectSort.js); the pinned
  // unsaved-session row goes above all of them.
  void ProjectsDialog::buildSortedRows(const core::ProjectsStore& store) {
    const QHash<QString, qint64> opened = fileStore::loadProjectOpened();
    std::vector<ProjectSortItem> entries;
    for (int i = 0; i < static_cast<int>(projects.size()); ++i) {
      const auto& m = projects[i].meta;
      const QString id = QString::fromStdString(m.id);
      entries.push_back({ false, i, "|" + id, QString::fromStdString(m.name).toLower(),
                          static_cast<long long>(m.updatedAt), opened.value(id, 0) });
    }
    for (int i = 0; i < remote.size(); ++i) {
      const auto& sp = remote[i];
      entries.push_back({ true, i, sp.serverUrl + "|" + sp.id, sp.name.toLower(), static_cast<long long>(sp.createdAt), 0 });
    }
    sortProjectItems(entries, g_projectsSortMode, g_projectsManualOrder);
    // No id (browser parity): no open, rename, checkbox, drag or "⋯"; a click closes the list (ProjectsOpen.cpp).
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

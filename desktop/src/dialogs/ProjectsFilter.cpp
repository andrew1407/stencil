#include "ProjectsDialog.hpp"

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "../support/SearchCombo.hpp"
#include "ProjectsDialog.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/filterFade.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "ShimmerOverlay.hpp"

// The filter box: its options, its fade and the kebab hover it shares.

namespace stencil::gui {

  // Hide rows the storage filter excludes. Placeholders (no project id) always show.
  void ProjectsDialog::rebuildFilterOptions() {
    if (!filter) return;
    const QString prev = filter->currentData().toString();  // preserve the selection
    filter->blockSignals(true);
    filter->clear();
    filter->addItem(tr("All"), "all");
    filter->addItem(tr("Local"), "local");
    const QStringList urls = connections ? connections->urls() : QStringList();
    if (!urls.isEmpty()) {
      filter->addItem(tr("All servers"), "server");
      for (const QString& u : urls) filter->addItem(u, u);  // one entry per specific server URL
    }
    knownServerUrls = urls;
    const int idx = filter->findData(prev.isEmpty() ? QStringLiteral("all") : prev);
    filter->setCurrentIndex(idx < 0 ? 0 : idx);
    filter->blockSignals(false);
  }

  // The filter/sort/search transition (support/filterFade): an excluded row fades and collapses its
  // slot, an included one plays that backwards. Lighter than the scatter - filtered is not deleted.
  ListFilterFade* ProjectsDialog::getFilterFade() {
    if (filterFade || !list) return filterFade;
    filterFade = new ListFilterFade(list);
    // setData fires itemChanged; the check-state bookkeeping must ignore our frames.
    filterFade->beforeFrame = [this] { building = true; };
    filterFade->afterFrame = [this] {
      building = false;
      list->viewport()->update();   // the delegate paints the fade; setData relayouts
    };
    // …and each row that is LEFT arrives out of sand as well as a fade — the shared
    // ListFilterFade::dustRowIn (browser js/ui/motion.js filterDust).
    filterFade->onArrive = [this](QListWidgetItem* it) { filterFade->dustRowIn(it, window()); };
    return filterFade;
  }

  void ProjectsDialog::applyFilter() {
    if (!filter) return;
    const QString mode = filter->currentData().toString();
    const QString needle = search ? search->text().trimmed() : QString();
    const QString smode = searchModeCombo ? searchModeCombo->currentData().toString()
                                           : QStringLiteral("common");
    auto wanted = [&](QListWidgetItem* it) {
      if (it->data(TEMP_ROLE).toBool()) {   // this window's session: a local thing, by name
        if (mode != QLatin1String("all") && mode != QLatin1String("local")) return false;
        return needle.isEmpty() || it->data(Qt::UserRole + 3).toString().contains(needle, Qt::CaseInsensitive);
      }
      if (it->data(Qt::UserRole).isNull()) return true;  // "Loading…"/"No projects" placeholders
      const QString srv = it->data(Qt::UserRole + 1).toString();
      const bool remote = !srv.isEmpty();
      bool show = true;
      if (mode == "local") show = !remote;
      else if (mode == "server") show = remote;          // any server
      else if (mode != "all") show = (srv == mode);      // a specific server URL
      if (show && !needle.isEmpty()) {                   // name / keyword search (case-insensitive)
        const QString name = it->data(Qt::UserRole + 3).toString();
        const QString kw = it->data(Qt::UserRole + 5).toString();
        const bool nameHit = name.contains(needle, Qt::CaseInsensitive);
        const bool kwHit = kw.contains(needle, Qt::CaseInsensitive);
        show = smode == "names" ? nameHit : smode == "keywords" ? kwHit : (nameHit || kwHit);
      }
      return show;
    };
    if (auto* fade = getFilterFade()) fade->apply(wanted);
    updateBatchBar();   // the filtered view IS the select-all pool (and the bar's reason to show)
  }

  // Point the delegate at the row whose "..." is under the cursor and sweep the app's own glass
  // shimmer across the chip (support/ShimmerOverlay.hpp, 325ms InOutSine), once per entry.
  void ProjectsDialog::setKebabHover(int row) {
    if (row == hover.kebabHoverRow) return;
    hover.kebabHoverRow = row;
    auto* del = static_cast<ProjectRowDelegate*>(list->itemDelegate());
    if (del) del->setKebabHover(row);
    list->viewport()->update();
    if (row < 0) { if (hover.kebabSweep) hover.kebabSweep->cancel(); return; }
    if (!hover.kebabSweep)
      hover.kebabSweep = new gui::ShimmerOverlay(nullptr, list, /*externalBands=*/true);
    if (QListWidgetItem* it = list->item(row))
      hover.kebabSweep->sweepBand(del ? del->kebabChipFor(list->visualItemRect(it)) : QRect());
  }

}  // namespace stencil::gui

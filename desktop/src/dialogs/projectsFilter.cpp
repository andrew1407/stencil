#include "projectsDialog.hpp"

#include "projectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "../support/searchCombo.hpp"
#include "projectsDialog.hpp"
#include "../support/flowLayout.hpp"
#include "../support/filterFade.hpp"
#include "../support/shimmerOverlay.hpp"
#include "shimmerOverlay.hpp"

// The filter box: its options, its fade and the kebab hover it shares.

namespace stencil::gui {

  // Hide rows the storage filter excludes. Placeholders (no project id) always show.
  void ProjectsDialog::rebuildFilterOptions() {
    if (!filter_) return;
    const QString prev = filter_->currentData().toString();  // preserve the selection
    filter_->blockSignals(true);
    filter_->clear();
    filter_->addItem(tr("All"), "all");
    filter_->addItem(tr("Local"), "local");
    const QStringList urls = connections_ ? connections_->urls() : QStringList();
    if (!urls.isEmpty()) {
      filter_->addItem(tr("All servers"), "server");
      for (const QString& u : urls) filter_->addItem(u, u);  // one entry per specific server URL
    }
    knownServerUrls_ = urls;
    const int idx = filter_->findData(prev.isEmpty() ? QStringLiteral("all") : prev);
    filter_->setCurrentIndex(idx < 0 ? 0 : idx);
    filter_->blockSignals(false);
  }

  // The filter/sort/search transition: an excluded row fades and collapses its slot,
  // an included one plays that backwards (support/filterFade). Deliberately lighter and
  // quicker than the removal scatter — filtered out is not deleted.
  ListFilterFade* ProjectsDialog::filterFade() {
    if (filterFade_ || !list_) return filterFade_;
    filterFade_ = new ListFilterFade(list_);
    // setData fires itemChanged; the check-state bookkeeping must ignore our frames.
    filterFade_->beforeFrame = [this] { building_ = true; };
    filterFade_->afterFrame = [this] {
      building_ = false;
      list_->viewport()->update();   // the delegate paints the fade; setData relayouts
    };
    // …and each row that is LEFT arrives out of sand as well as a fade — the shared
    // ListFilterFade::dustRowIn (browser js/ui/motion.js filterDust).
    filterFade_->onArrive = [this](QListWidgetItem* it) { filterFade_->dustRowIn(it, window()); };
    return filterFade_;
  }

  void ProjectsDialog::applyFilter() {
    if (!filter_) return;
    const QString mode = filter_->currentData().toString();
    const QString needle = search_ ? search_->text().trimmed() : QString();
    const QString smode = searchModeCombo_ ? searchModeCombo_->currentData().toString()
                                           : QStringLiteral("common");
    auto wanted = [&](QListWidgetItem* it) {
      if (it->data(kTempRole).toBool()) {   // this window's session: a local thing, by name
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
    if (auto* fade = filterFade()) fade->apply(wanted);
    updateBatchBar();   // the filtered view IS the select-all pool (and the bar's reason to show)
  }

  // Point the delegate at the row whose "⋯" is under the cursor, and sweep the app's own
  // glass shimmer across the chip as the pointer arrives — the same 325ms InOutSine band
  // every other control plays (support/shimmerOverlay.hpp), once per entry.
  void ProjectsDialog::setKebabHover(int row) {
    if (row == kebabHoverRow_) return;
    kebabHoverRow_ = row;
    auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    if (del) del->setKebabHover(row);
    list_->viewport()->update();
    if (row < 0) { if (kebabSweep_) kebabSweep_->cancel(); return; }
    if (!kebabSweep_)
      kebabSweep_ = new gui::ShimmerOverlay(nullptr, list_, /*externalBands=*/true);
    if (QListWidgetItem* it = list_->item(row))
      kebabSweep_->sweepBand(del ? del->kebabChipFor(list_->visualItemRect(it)) : QRect());
  }

}  // namespace stencil::gui

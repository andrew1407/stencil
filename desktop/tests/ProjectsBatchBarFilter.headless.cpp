// A filter change as a question re-answered: the excluded row is gone at once and the matching one
// re-forms out of the filter's own light sand. Then a re-list landing mid-flight, and the footer row.
#include "ProjectsBatchBarParts.hpp"

int filterTransitions(const std::vector<Project>& locals) {
  // Filter transitions: a row the SEARCH excludes fades and collapses out (support/filterFade), never
  // with the scatter a removal uses, and comes back the same way round. Local-only: no server poll.
  {
    std::printf("filter transitions (the answer arrives; nothing plays out):\n");
    ProjectsDialog dlg(locals, 5000);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    auto* search = dlg.findChild<QLineEdit*>();
    check(list && search, "finds the projects list and its search box");
    if (!list || !search) return 1;
    auto keep = [&] { return rowById(list, "l1", false); };   // "alpha" — matches "alp"
    auto drop = [&] { return rowById(list, "l2", false); };   // "beta"  — does not
    auto rowH = [&](QListWidgetItem* it) { return it ? list->visualItemRect(it).height() : -1; };
    check(keep() && drop(), "finds the rows the search keeps and drops");
    if (!keep() || !drop()) { dlg.reject(); return 1; }
    const int fullH = rowH(keep());
    check(fullH > 0 && rowH(drop()) == fullH, "a settled row occupies its whole slot");

    search->setText("alp");
    // A filter DROPS nothing: the excluded row is out of the answer, and out of the view,
    // the moment the answer changes. There is no exit to watch.
    check(drop()->isHidden() && rowH(drop()) == 0, "an excluded row is gone at once");
    check(!filteredIn(drop()), "…and has left the filtered set");
    check(filteredIn(keep()), "…while the matching row is still in it");
    check(dlg.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) == nullptr,
          "…with none of the destructive scatter a removal uses");
    // The whole effect belongs to what is LEFT: the matching row keeps its slot (nothing may jump under
    // the pointer) and re-forms out of the filter's own lighter sand, named apart from a removal's.
    check(rowH(keep()) == fullH, "the matching row keeps its slot — no jump under the cursor");
    check(dlg.findChild<QWidget*>(QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) != nullptr,
          "…and arrives out of the filter's own dust");
    pumpUntil([&] { return dlg.findChild<QWidget*>(
                        QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) == nullptr; });
    check(!keep()->isHidden() && rowH(keep()) == fullH, "…landing whole, where it always was");

    search->clear();
    check(!drop()->isHidden(), "a revealed row is in the view at once");
    check(filteredIn(drop()), "…and already counted in the filtered set");
    pumpUntil([&] { return rowH(drop()) == fullH; });
    check(rowH(drop()) == fullH, "…and its slot opens to full height");

    // Rapid edits: the LAST needle decides, with nothing left stuck part-collapsed.
    for (const char* q : {"al", "be", "ga", "a", ""}) {
      search->setText(QString::fromLatin1(q));
      pumpFor(FILTER_FADE_MS / 6);   // each edit interrupts the transition before it
    }
    pumpUntil([&] {
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->isHidden() || rowH(list->item(i)) != fullH) return false;
      return true;
    });
    bool allBack = true;
    for (int i = 0; i < list->count(); ++i)
      allBack = allBack && !list->item(i)->isHidden() && filteredIn(list->item(i)) &&
                rowH(list->item(i)) == fullH;
    check(allBack, "an empty search leaves every row whole after a rapid sequence");

    // Reduced motion: the same result, reached with no transition at all.
    qputenv("STENCIL_NO_ANIM", "1");
    search->setText("alp");
    check(drop()->isHidden() && rowH(drop()) == 0,
          "reduced motion hides the excluded row at once, slot closed");
    check(!keep()->isHidden() && rowH(keep()) == fullH, "…and leaves the match untouched");
    search->clear();
    check(!drop()->isHidden() && rowH(drop()) == fullH,
          "…restoring it whole, still without animating");
    qunsetenv("STENCIL_NO_ANIM");
    dlg.reject();
  }

  // A live re-list landing mid dust-flight must not crash: setProjects() does list->clear(), so a
  // pending veil-lift timer must not touch the QListWidgetItem it started with.
  {
    std::printf("a live re-list mid dust-flight (crash regression):\n");
    ProjectsDialog dlg(locals, 5000);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    auto* search = dlg.findChild<QLineEdit*>();
    check(list && search, "finds the projects list and its search box");
    if (!list || !search) return 1;
    search->setText("alp");   // "alpha" arrives out of dust; its veil-lift timer is now pending
    check(dlg.findChild<QWidget*>(QString::fromLatin1(FILTER_DUST_OBJECT_NAME)) != nullptr,
          "dust is in flight for the arriving row");
    dlg.setProjects(locals);   // the live re-list — clears and rebuilds every row mid-flight
    // Pump well past the veil-lift timer (FILTER_DUST_MS * FILTER_DUST_VEIL_STOP) without
    // crashing — that's the whole regression.
    pumpFor(400);
    check(dlg.isVisible(), "the dialog survives a re-list landing mid dust-flight");
    check(rowById(list, "l1", false) != nullptr, "the rebuilt list still has its rows");
    dlg.reject();
  }

  // The footer row never overlaps itself (support/modalChrome addModalFooter): the hint sits beside the
  // buttons or, once the row cannot hold the hint's floor, on its own line above them — never on top.
  {
    std::printf("the footer hint never runs under its buttons:\n");
    const QFont appFont = QApplication::font();
    for (double scale : {1.0, 1.6}) {
      QFont f = appFont;
      f.setPointSizeF(appFont.pointSizeF() * scale);
      QApplication::setFont(f);
      ProjectsDialog dlg(locals, 5000);
      dlg.show();
      pumpFor(80);   // the minimum-width pass runs a turn after the buttons are added
      auto* hint = dlg.findChild<QLabel*>("modalFooterHint");
      auto* first = btnByText(&dlg, "Blank image");
      check(hint && first, "finds the footer hint and its first button");
      if (hint && first) {
        const QRect h(hint->mapTo(&dlg, QPoint(0, 0)), hint->size());
        const QRect b(first->mapTo(&dlg, QPoint(0, 0)), first->size());
        check(h.right() < b.left() || h.bottom() < b.top(),
              "the hint ends before the first button, or sits wholly above it");
        // …and that button still has room for its own label, not just its icon.
        check(first->width() >= first->minimumSizeHint().width(),
              "the first footer button keeps its natural width");
        check(h.width() > 0 && h.height() > 0, "the hint is not squeezed away");
      }
      dlg.reject();
    }
    QApplication::setFont(appFont);
  }
  return 0;
}

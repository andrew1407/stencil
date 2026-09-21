// A row the viewport cuts dissolves instead of being sliced.
#include "connectRowParts.hpp"

namespace connectrow {

  void checkScrollEdges(quint16 port, QListWidget* list, QWidget* row) {
  // ── Scroll edges: a row the viewport cuts dissolves instead of being sliced
  // across its outline (projects-list parity).
  {
    ConnectionManager many;
    QString e;
    for (int i = 0; i < 8; ++i)
      stencil::test::connectNow(many, QStringLiteral("http://row%1@127.0.0.1:%2").arg(i).arg(port), QString(), e);
    ConnectDialog tall(&many);
    // Shorter than eight rows, so the list has to scroll — but tall enough that a few
    // fit whole below the modal chrome (header pill + footer hint) the dialog now wears.
    tall.resize(560, 540);
    tall.show();
    pumpFor(120);
    auto* tl = tall.findChild<QListWidget*>(QStringLiteral("connList"));
    check(tl != nullptr && tl->count() == 8, "eight rows in the short list");
    if (tl && tl->count() == 8) {
      check(tl->verticalScrollBar()->maximum() > 0, "…which therefore scrolls");
      const int viewH = tl->viewport()->height();
      int whole = -1, cut = -1;
      for (int i = 0; i < tl->count(); ++i) {
        QWidget* w = tl->itemWidget(tl->item(i));
        if (!w) continue;
        const int top = w->mapTo(tl->viewport(), QPoint(0, 0)).y();
        if (top >= 0 && top + w->height() <= viewH) { if (whole < 0) whole = i; }
        else if (top < viewH && top + w->height() > viewH) cut = i;
      }
      check(whole >= 0 && cut >= 0, "…with both a whole row and one the bottom edge cuts");
      if (whole >= 0 && cut >= 0) {
        auto* wholeFx =
            dynamic_cast<DissolveEffect*>(tl->itemWidget(tl->item(whole))->graphicsEffect());
        auto* cutFx =
            dynamic_cast<DissolveEffect*>(tl->itemWidget(tl->item(cut))->graphicsEffect());
        check(!wholeFx || wholeFx->getDissolve() <= 0.0, "a fully visible row is not dissolved");
        check(cutFx && cutFx->getDissolve() > 0.0, "…while the clipped row fades at the edge");
      }
    }

    // The FILTER's own transition, over the same eight rows: what it excludes is gone at once
    // (support/filterFade) since nothing was disconnected, and the rows that are LEFT arrive.
    auto* kind = tall.findChild<QComboBox*>(QStringLiteral("connKindFilter"));
    check(kind != nullptr, "the tall list carries the kind filter");
    if (tl && kind && tl->count() == 8) {
      const int fullH = tl->item(0)->data(FILTER_FULL_HEIGHT_ROLE).toInt();
      check(fullH > 0, "rows record the slot height a filter opens");
      // Every one of these is non-admin, so "Admin" empties the whole list.
      kind->setCurrentIndex(kind->findData(QStringLiteral("admin")));
      QWidget* w0 = tl->itemWidget(tl->item(0));
      bool allGone = true;
      for (int i = 0; i < 8; ++i)
        allGone = allGone && tl->item(i)->isHidden() && tl->item(i)->sizeHint().height() == 0;
      check(allGone, "Admin empties a list of non-admin rows at once, every slot closed");
      check(w0 && !w0->property(FILTER_FADE_PROPERTY).toBool(),
            "…owning no fade: a row that is out of the answer has nothing to play");
      check(w0 && w0->graphicsEffect() == nullptr,
            "…and no stale effect either, so the scroll-edge reveal gets it back");
      check(tall.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) == nullptr,
            "…and it spends none of the removal's dust");
      check(tl->count() == 9 && tl->item(8)->text().contains(QStringLiteral("admin credential")),
            "…and the explanatory line sits after them");

      // Settled = visible, full slot, and the fade has handed the widget back (the last
      // pixel of height rounds up a tick before presence actually lands on 1).
      auto settled = [&] {
        for (int i = 0; i < 8; ++i) {
          if (tl->item(i)->isHidden() || tl->item(i)->sizeHint().height() != fullH) return false;
          QWidget* w = tl->itemWidget(tl->item(i));
          if (w && w->property(FILTER_FADE_PROPERTY).toBool()) return false;
        }
        return true;
      };
      kind->setCurrentIndex(kind->findData(QStringLiteral("all")));
      check(!tl->item(0)->isHidden() && tl->item(0)->sizeHint().height() < fullH,
            "…and the rows that are left arrive, still expanding");
      pumpFor(FILTER_FADE_MS / 4);
      check(w0 && w0->property(FILTER_FADE_PROPERTY).toBool(),
            "an arriving row owns its own fade while it comes in");
      check(w0 && dynamic_cast<QGraphicsOpacityEffect*>(w0->graphicsEffect()) != nullptr,
            "…a plain opacity fade, not the scroll edge's grain");
      check(w0 && dynamic_cast<DissolveEffect*>(w0->graphicsEffect()) == nullptr,
            "…so the two motions never fight over one effect");
      pumpUntil(settled);
      check(settled(), "every row returns to its full slot");
      check(w0 && !w0->property(FILTER_FADE_PROPERTY).toBool() &&
                dynamic_cast<QGraphicsOpacityEffect*>(w0->graphicsEffect()) == nullptr,
            "…handing its opacity back to the scroll-edge reveal");

      // Rapid changes: whatever is mid-flight, the LAST pick decides the visible set.
      for (const char* mode : {"admin", "nonadmin", "admin", "all"}) {
        kind->setCurrentIndex(kind->findData(QString::fromLatin1(mode)));
        pumpFor(FILTER_FADE_MS / 6);   // each flip interrupts the one before it
      }
      pumpUntil(settled);
      check(settled(), "no row is stuck hidden or part-collapsed after a rapid sequence");
      check(tl->count() == 8, "…and no stale explanatory line is left behind");

      // Reduced motion: the same result, reached with no transition at all.
      qputenv("STENCIL_NO_ANIM", "1");
      kind->setCurrentIndex(kind->findData(QStringLiteral("admin")));
      bool instantGone = true;
      for (int i = 0; i < 8; ++i)
        instantGone = instantGone && tl->item(i)->isHidden() && tl->item(i)->sizeHint().height() == 0;
      check(instantGone, "reduced motion filters straight to the end state");
      kind->setCurrentIndex(kind->findData(QStringLiteral("all")));
      check(settled(), "…and restores every row the same way");
      qunsetenv("STENCIL_NO_ANIM");
    }
  }

  }

}  // namespace connectrow

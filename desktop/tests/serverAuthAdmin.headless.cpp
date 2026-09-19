// The admin row: what it may do, and what it must refuse.
#include "serverAuthParts.hpp"

namespace serverauth {

  void checkAdminRow(MockServer& mock, MockServer& mock2) {
  // ── the ADMIN row: golden band + tooltip, Invite gated on the kind, kind filter ──
  std::printf("admin row:\n");
  {
    mock.goodBearer = "sess-r";
    mock.mintToken = "sess-r";
    mock.mintBearer = "admin-token";
    mock2.goodBearer = "plain-tok";
    ConnectionManager mgr;
    QString err;
    check(stencil::test::connectNow(mgr, mock.url(), QStringLiteral("admin-token"), err),
          "an admin connection for the row round");
    check(stencil::test::connectNow(mgr, mock2.url(), QStringLiteral("plain-tok"), err),
          "…and a non-admin one beside it");
    ConnectDialog dlg(&mgr);
    dlg.resize(560, 460);
    dlg.show();
    pumpFor(60);

    const auto gold = dlg.findChildren<QWidget*>(QStringLiteral("connRowAdmin"));
    check(gold.size() == 1, "exactly the ADMIN row wears the golden band");
    auto* rowList = dlg.findChild<QListWidget*>(QStringLiteral("connList"));
    if (!gold.isEmpty() && rowList) {
      // The card styles live on the LIST (one cascading sheet); the row carries the
      // objectName its rule selects.
      check(rowList->styleSheet().contains(
                QStringLiteral("QWidget#connRowAdmin{border:2px solid #d4a017")),
            "…drawn in the app's collaboration gold");
      check(gold[0]->toolTip().contains(QStringLiteral("mint session tokens")),
            "…and saying on its tooltip what the credential can do");
      // The lock + "Admin" pair beside the URL, at its natural width — a QLabel host
      // measured itself from its own empty text and squeezed the pair to a sliver.
      auto* badge = dlg.findChild<QWidget*>(QStringLiteral("connAdminBadge"));
      check(badge != nullptr, "…and carrying the Admin badge");
      if (badge) {
        int parts = 0;
        for (QLabel* l : badge->findChildren<QLabel*>()) parts += l->sizeHint().width();
        check(parts > 0 && badge->sizeHint().width() >= parts,
              "…wide enough for the lock glyph and the word, not a sliver");
      }
      // Browser .connect-row parity: the plain rows are filled cards too — radius 8,
      // hovering to --bg-info.
      check(rowList->styleSheet().contains(QStringLiteral("border-radius:8px")) &&
                rowList->styleSheet().contains(QStringLiteral("QWidget#connRow[hovered=\"true\"]")),
            "…and every row is a rounded card with a hover wash");
    }
    // The Invite button follows the KIND now, not merely "has a credential": a session
    // token cannot mint (the server 401s it), so only the admin row offers one.
    check(dlg.findChildren<QPushButton*>(QStringLiteral("inviteBtn")).size() == 1,
          "only the admin row offers Invite");

    auto* filter = dlg.findChild<QComboBox*>(QStringLiteral("connKindFilter"));
    auto* lw = dlg.findChild<QListWidget*>(QStringLiteral("connList"));
    check(filter != nullptr, "the list carries an All / Admin / Non-admin filter");
    check(lw != nullptr, "…over the connections list");
    const QStringList urls = mgr.urls();
    const int adminRow = urls.indexOf(ServerClient::normalizeBase(mock.url()));
    const int plainRow = urls.indexOf(ServerClient::normalizeBase(mock2.url()));
    check(adminRow >= 0 && plainRow >= 0, "both rows are in the list");
    if (filter && lw && adminRow >= 0 && plainRow >= 0) {
      check(filter->count() == 3 && filter->currentData().toString() == QLatin1String("all"),
            "…three ways, All by default");
      check(!lw->item(adminRow)->isHidden() && !lw->item(plainRow)->isHidden(),
            "…which shows every row");
      const int fullH = lw->item(plainRow)->data(FILTER_FULL_HEIGHT_ROLE).toInt();
      check(fullH > 0 && lw->item(plainRow)->sizeHint().height() == fullH,
            "a settled row occupies its whole slot");

      // A filter is a QUESTION re-answered, not a removal: the row it excludes was never disconnected, so
      // there is no exit to watch — it leaves the view at once and the surviving row keeps the slot it had.
      filter->setCurrentIndex(filter->findData(QStringLiteral("admin")));
      check(lw->item(plainRow)->isHidden() && lw->item(plainRow)->sizeHint().height() == 0,
            "an excluded row is gone at once, slot closed");
      check(!filteredIn(lw->item(plainRow)), "…and has left the filtered set");
      check(filteredIn(lw->item(adminRow)), "…while the matching row is still in it");
      check(!lw->item(adminRow)->isHidden() && lw->item(adminRow)->sizeHint().height() == fullH,
            "Admin leaves the surviving row where it was, at full height");
      // The semantics: a filter-out is NOT a removal, so it never spends the dust.
      check(dlg.findChild<QWidget*>(DisintegrateOverlay::OBJECT_NAME) == nullptr,
            "…with none of the destructive scatter a disconnect uses");

      // ── …and a row the filter REVEALS opens its slot: up at once, still growing.
      filter->setCurrentIndex(filter->findData(QStringLiteral("nonadmin")));
      check(!lw->item(plainRow)->isHidden() && lw->item(plainRow)->sizeHint().height() < fullH,
            "a revealed row is in the view at once, still expanding");
      check(filteredIn(lw->item(plainRow)), "…and already counted in the filtered set");
      check(lw->item(adminRow)->isHidden(), "Non-admin hides the admin row, at once");
      pumpUntil([&] { return lw->item(plainRow)->sizeHint().height() == fullH; });
      check(lw->item(plainRow)->sizeHint().height() == fullH,
            "…with the arrived row landing on its full slot");

      // ── Rapid changes: whatever is mid-flight, the LAST pick decides the visible set.
      for (const char* mode : {"admin", "nonadmin", "admin", "all"}) {
        filter->setCurrentIndex(filter->findData(QString::fromLatin1(mode)));
        pumpFor(FILTER_FADE_MS / 6);   // each flip interrupts the one before it
      }
      pumpUntil([&] {
        return !lw->item(adminRow)->isHidden() && !lw->item(plainRow)->isHidden() &&
               lw->item(adminRow)->sizeHint().height() == fullH &&
               lw->item(plainRow)->sizeHint().height() == fullH;
      });
      check(!lw->item(adminRow)->isHidden() && !lw->item(plainRow)->isHidden(),
            "…and All brings both back");
      check(lw->item(adminRow)->sizeHint().height() == fullH &&
                lw->item(plainRow)->sizeHint().height() == fullH,
            "no row is left stuck part-collapsed after a rapid sequence");
      for (int i = 0; i < lw->count(); ++i)
        check(filteredIn(lw->item(i)) == !lw->item(i)->isHidden(),
              "…and every row's hidden state agrees with the filtered set");
    }

    // Nothing matching says so, instead of leaving an empty list unexplained.
    ConnectionManager lone;
    check(stencil::test::connectNow(lone, mock2.url(), QStringLiteral("plain-tok"), err),
          "a lone non-admin connection");
    ConnectDialog dlg2(&lone);
    dlg2.resize(560, 460);
    dlg2.show();
    pumpFor(60);
    auto* f2 = dlg2.findChild<QComboBox*>(QStringLiteral("connKindFilter"));
    auto* l2 = dlg2.findChild<QListWidget*>(QStringLiteral("connList"));
    if (f2 && l2) {
      f2->setCurrentIndex(f2->findData(QStringLiteral("admin")));
      QListWidgetItem* last = l2->count() ? l2->item(l2->count() - 1) : nullptr;
      check(l2->count() == 2 && last && !last->isHidden() &&
                last->text().contains(QStringLiteral("admin credential")),
            "a filter that matches nothing shows an explanatory line");
      f2->setCurrentIndex(f2->findData(QStringLiteral("all")));
      check(l2->count() == 1 && !l2->item(0)->isHidden(),
            "…which leaves again once rows match");
      pumpUntil([&] { return !l2->item(0)->isHidden(); });

      // ── Reduced motion: the same result, reached with no transition at all.
      qputenv("STENCIL_NO_ANIM", "1");
      f2->setCurrentIndex(f2->findData(QStringLiteral("admin")));
      check(l2->item(0)->isHidden(), "reduced motion hides the excluded row at once");
      check(l2->item(0)->sizeHint().height() == 0, "…with its slot already closed");
      f2->setCurrentIndex(f2->findData(QStringLiteral("all")));
      check(!l2->item(0)->isHidden() &&
                l2->item(0)->sizeHint().height() ==
                    l2->item(0)->data(FILTER_FULL_HEIGHT_ROLE).toInt(),
            "…and restores it whole, still without animating");
      qunsetenv("STENCIL_NO_ANIM");
    }
    mock.mintBearer.clear();
    mock.goodBearer.clear();
    mock2.goodBearer.clear();
    mock.mintToken = "tok";
  }

  }

}  // namespace serverauth

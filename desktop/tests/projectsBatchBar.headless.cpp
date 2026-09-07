// Headless checks for the Projects dialog's multi-select surface (dialogs/projectsDialog):
//   - batch bar composition: the Move/Copy direction buttons are HIDDEN (not greyed)
//     unless the selection is homogeneous — local-only (and a server is connected)
//     shows Move/Copy to server, server-only shows Move/Copy to local, mixed shows
//     neither (browser parity: projectsModal.js updateBatchBar);
//   - the Select all / Deselect all toggle sweeps the CURRENT filtered view only,
//     flips its label once everything visible is checked, and deselect clears the
//     whole selection (browser: the per-render `selectables` pool);
//   - batch remove confirms in-dialog (the dialog stays open) and resolves every checked
//     row: each VISIBLE row's clipped rect gets a DisintegrateOverlay inside the list
//     viewport (never over the dialog chrome), rows scrolled out of view spawn none, and
//     every checked id is retired in place while removeRequested carries the batch;
//   - a filter/search change is a QUESTION re-answered (support/filterFade): what it
//     excludes is gone at once with nothing to watch, and the rows that are LEFT arrive —
//     keeping their slots if they had them — out of the filter's own light sand, never
//     the removal's destructive scatter, which is named apart so the two can't be
//     confused; reduced motion goes straight to the end, and rapid edits land the right
//     visible set.
// A mock QTcpServer stands in for the collaboration server (token + /projects list),
// so the server-row compositions run without a Go server. Offscreen, like the others.
#include "controlReveal.hpp"   // kControlRevealInMs: the batch group's slot timing
#include "disintegrateOverlay.hpp"
#include "fileStore.hpp"
#include "filterFade.hpp"   // the light enter/exit transition the search/filter uses
#include "projectsDialog.hpp"
#include "serverClient.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdio>
#include <functional>

using stencil::gui::BatchDirections;
using stencil::gui::batchDirectionsFor;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::filteredIn;
using stencil::gui::kFilterDustObjectName;
using stencil::gui::kFilterFadeMs;
using stencil::gui::Project;
using stencil::gui::ProjectsDialog;
using stencil::net::ConnectionManager;

#include "support/check.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void pumpUntil(const std::function<bool()>& pred, int timeoutMs = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// The batch bar's buttons carry fixed labels — find each by its exact text.
static QPushButton* btnByText(QWidget* root, const QString& text) {
  for (QPushButton* b : root->findChildren<QPushButton*>())
    if (b->text() == text) return b;
  return nullptr;
}

static Project makeLocal(const QString& id, const QString& name, long long updatedAt) {
  Project pr;
  pr.meta.id = id.toStdString();
  pr.meta.name = name.toStdString();
  pr.meta.updatedAt = updatedAt;
  return pr;
}

static QListWidgetItem* rowById(QListWidget* list, const QString& id, bool remote) {
  for (int i = 0; i < list->count(); ++i) {
    QListWidgetItem* it = list->item(i);
    if (it->data(Qt::UserRole).toString() == id &&
        it->data(Qt::UserRole + 1).toString().isEmpty() != remote)
      return it;
  }
  return nullptr;
}

int main(int argc, char** argv) {
  setbuf(stdout, nullptr);  // a crash mid-run must not swallow the progress log
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("projectsBatchBarHeadless");


  // ── Pure composition rule (no widgets needed) ──
  std::printf("batch direction matrix:\n");
  check(batchDirectionsFor(2, 0, true).toServer && !batchDirectionsFor(2, 0, true).toLocal,
        "local-only + server: to-server only");
  check(!batchDirectionsFor(2, 0, false).toServer, "local-only, no server: to-server hidden");
  check(batchDirectionsFor(0, 2, false).toLocal && !batchDirectionsFor(0, 2, false).toServer,
        "server-only: to-local only (needs no connection gate)");
  check(!batchDirectionsFor(1, 1, true).toServer && !batchDirectionsFor(1, 1, true).toLocal,
        "mixed: both directions hidden");
  check(!batchDirectionsFor(0, 0, true).toServer && !batchDirectionsFor(0, 0, true).toLocal,
        "empty selection: both hidden");

  // ── Mock collaboration server: token + a two-project listing; 404 for the rest. ──
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "mock server listens");
  QObject::connect(&server, &QTcpServer::newConnection, [&] {
    while (QTcpSocket* s = server.nextPendingConnection()) {
      auto* buf = new QByteArray;
      QObject::connect(s, &QTcpSocket::readyRead, [s, buf] {
        buf->append(s->readAll());
        if (!buf->contains("\r\n\r\n")) return;  // wait for the full header block
        const QByteArray line = buf->left(buf->indexOf("\r\n"));
        QByteArray body = "{}";
        QByteArray status = "200 OK";
        if (line.startsWith("POST /auth/token")) {
          body = "{\"token\":\"tok\"}";
        } else if (line.startsWith("GET /projects ") || line.startsWith("GET /projects?")) {
          // hasImage matters: sharedProjectsAsync only surfaces image-bearing projects.
          body =
              "{\"projects\":[{\"id\":\"r1\",\"name\":\"delta\",\"hasImage\":true,"
              "\"imageW\":4,\"imageH\":4,\"createdAt\":100,\"version\":1},"
              "{\"id\":\"r2\",\"name\":\"epsilon\",\"hasImage\":true,"
              "\"imageW\":4,\"imageH\":4,\"createdAt\":200,\"version\":1}]}";
        } else {
          status = "404 Not Found";
        }
        s->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        buf->clear();
        s->flush();
        s->disconnectFromHost();  // may delete buf synchronously via disconnected
      });
      QObject::connect(s, &QTcpSocket::disconnected, [s, buf] { delete buf; s->deleteLater(); });
    }
  });
  const QString url = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());

  ConnectionManager mgr;
  QString err;
  check(mgr.connectTo(url, QString(), err), "connects to the mock server");

  std::vector<Project> locals;
  locals.push_back(makeLocal("l1", "alpha", 3000));
  locals.push_back(makeLocal("l2", "beta", 2000));
  locals.push_back(makeLocal("l3", "gamma", 1000));

  {
    std::printf("batch bar visibility by selection composition:\n");
    ProjectsDialog dlg(locals, 5000, &mgr);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list != nullptr, "finds the projects list");
    if (!list) return 1;
    pumpUntil([&] { return rowById(list, "r1", true) != nullptr; });
    check(rowById(list, "r1", true) != nullptr, "server rows arrived from the mock listing");
    if (!rowById(list, "r1", true)) { dlg.reject(); return 1; }

    QPushButton* toServer = btnByText(&dlg, QStringLiteral("Move to server"));
    QPushButton* copyServer = btnByText(&dlg, QStringLiteral("Copy to server"));
    QPushButton* toLocal = btnByText(&dlg, QStringLiteral("Move to local"));
    QPushButton* copyLocal = btnByText(&dlg, QStringLiteral("Copy to local"));
    QPushButton* removeBtn = btnByText(&dlg, QStringLiteral("Remove selected"));
    check(toServer && copyServer && toLocal && copyLocal && removeBtn,
          "finds all five batch buttons");
    if (!(toServer && copyServer && toLocal && copyLocal && removeBtn)) return 1;
    check(!toServer->isVisible() && !toLocal->isVisible(),
          "no selection: batch bar (and its buttons) hidden");

    // Local-only selection: to-server pair shown, to-local pair hidden entirely.
    rowById(list, "l1", false)->setCheckState(Qt::Checked);
    rowById(list, "l2", false)->setCheckState(Qt::Checked);
    check(toServer->isVisible() && copyServer->isVisible(),
          "local-only: To server / Server copy visible");
    check(!toLocal->isVisible() && !copyLocal->isVisible(),
          "local-only: To local / Local copy hidden (not greyed)");
    check(removeBtn->isVisible(), "Remove stays available for any selection");

    // Squeezed narrow, the bar WRAPS (browser .projects-batch-bar / -selected flex-wrap):
    // "Remove selected" drops to a line of its own instead of being cut off at the edge.
    {
      pumpFor(stencil::gui::kControlRevealInMs + 100);   // the group's slot has slid fully open
      dlg.setMinimumSize(0, 0);
      dlg.resize(420, dlg.height());
      pumpFor(80);
      const auto inDlg = [&dlg](QWidget* w) { return QRect(w->mapTo(&dlg, QPoint(0, 0)), w->size()); };
      bool inside = true;
      for (QPushButton* b : {toServer, copyServer, removeBtn})
        inside = inside && dlg.rect().contains(inDlg(b));
      check(inside, "narrow: every batch button stays inside the dialog");
      check(inDlg(removeBtn).top() > inDlg(toServer).top(),
            "narrow: Remove selected wraps onto a line under the first");
      dlg.resize(560, dlg.height());
      pumpFor(80);
    }

    // Mixed selection: every direction hidden, the bar itself stays up.
    rowById(list, "r1", true)->setCheckState(Qt::Checked);
    // The bar's buttons come and go as the app's control swap (ProjectsDialog
    // updateBatchBar -> support/controlReveal), and a hide only lands once its collapse
    // has played, so the settled state is what gets asserted.
    pumpUntil([&] {
      return !toServer->isVisible() && !copyServer->isVisible() && !toLocal->isVisible() &&
             !copyLocal->isVisible();
    });
    check(!toServer->isVisible() && !copyServer->isVisible() && !toLocal->isVisible() &&
              !copyLocal->isVisible(),
          "mixed: all four direction buttons hidden");
    check(removeBtn->isVisible(), "mixed: Remove still visible");

    // Server-only selection: the to-local pair appears, to-server pair hides.
    rowById(list, "l1", false)->setCheckState(Qt::Unchecked);
    rowById(list, "l2", false)->setCheckState(Qt::Unchecked);
    check(toLocal->isVisible() && copyLocal->isVisible(),
          "server-only: To local / Local copy visible");
    pumpUntil([&] { return !toServer->isVisible() && !copyServer->isVisible(); });
    check(!toServer->isVisible() && !copyServer->isVisible(),
          "server-only: To server / Server copy hidden");

    // ── Select all / deselect all over the filtered view ──
    std::printf("select-all over the filtered view:\n");
    auto* selectAll = dlg.findChild<QPushButton*>("projectsSelectAll");
    auto* search = dlg.findChild<QLineEdit*>();
    check(selectAll && search, "finds the select-all button and the search box");
    if (!selectAll || !search) return 1;
    rowById(list, "r1", true)->setCheckState(Qt::Unchecked);  // start clean
    check(selectAll->isVisible(), "select-all visible while the list has selectable rows");
    check(selectAll->text() == QStringLiteral("Select all"), "label starts as Select all");

    selectAll->click();
    int checkedRows = 0;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->checkState() == Qt::Checked) ++checkedRows;
    check(checkedRows == 5, "select-all checks every row of the unfiltered view");
    check(selectAll->text() == QStringLiteral("Deselect all"),
          "label flips to Deselect all once everything visible is checked");

    selectAll->click();  // deselect clears the whole selection
    checkedRows = 0;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->checkState() == Qt::Checked) ++checkedRows;
    check(checkedRows == 0, "deselect-all clears the selection");
    check(selectAll->text() == QStringLiteral("Select all"), "label flips back to Select all");

    // A filtered select-all must not sweep up rows the user cannot see.
    search->setText("alp");  // matches only the local "alpha"
    pumpFor(20);
    check(selectAll->isVisible(), "select-all still shown for a non-empty filtered view");
    selectAll->click();
    check(rowById(list, "l1", false)->checkState() == Qt::Checked, "filtered row got checked");
    check(rowById(list, "l2", false)->checkState() == Qt::Unchecked &&
              rowById(list, "r1", true)->checkState() == Qt::Unchecked,
          "rows outside the filter stay unchecked");
    check(selectAll->text() == QStringLiteral("Deselect all"),
          "the filtered view counts as fully selected");
    search->clear();
    pumpFor(20);
    check(selectAll->text() == QStringLiteral("Select all"),
          "clearing the filter grows the pool — label back to Select all");
    check(rowById(list, "l1", false)->checkState() == Qt::Checked,
          "the filtered selection survives clearing the filter");

    search->setText("zzz-no-match");  // empty filtered view → nothing to select
    pumpFor(20);
    pumpUntil([&] { return !selectAll->isVisible(); });   // …once its swap has played
    check(!selectAll->isVisible(), "select-all hides when the filtered view is empty");
    dlg.reject();
  }

  // ── Batch remove: every checked row resolves; visible rows scatter inside the list ──
  {
    std::printf("batch remove scatter preconditions:\n");
    std::vector<Project> many;
    for (int i = 0; i < 14; ++i)
      many.push_back(makeLocal(QString("m%1").arg(i), QString("proj-%1").arg(i), 1000 + i));
    ProjectsDialog dlg(many, 5000);   // no server needed here
    dlg.resize(760, 320);             // small: a good half of the rows scroll out of view
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list && list->count() == 14, "all 14 rows built");
    if (!list) return 1;

    // Check every row (also the scrolled-out ones), then count what is actually visible.
    for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Checked);
    const QRect view = list->viewport()->rect();
    int expectedOverlays = 0;
    for (int i = 0; i < list->count(); ++i) {
      const QRect r = list->visualItemRect(list->item(i)).intersected(view);
      if (r.width() >= 8 && r.height() >= 8) ++expectedOverlays;
    }
    check(expectedOverlays > 0, "some checked rows are on screen");
    check(expectedOverlays < list->count(), "some checked rows are scrolled out of view");
    const QRect viewInDlg(list->viewport()->mapTo(&dlg, QPoint(0, 0)), view.size());

    QPushButton* removeBtn = btnByText(&dlg, QStringLiteral("Remove selected"));
    check(removeBtn && removeBtn->isVisible(), "batch Remove available with 14 checked");
    if (!removeBtn) return 1;
    // The confirm now shows INSIDE the still-open dialog (the chrome-styled
    // confirmModal); answer Confirm from a 0-timer (it fires within the modal's
    // own event loop) and collect the emitted items.
    QVector<QPair<QString, QString>> removed;
    QObject::connect(&dlg, &ProjectsDialog::removeRequested,
                     [&removed](const QVector<QPair<QString, QString>>& items) {
                       removed = items;
                     });
    QTimer::singleShot(0, [] {
      for (int i = 0; i < 200; ++i) {
        QWidget* m = QApplication::activeModalWidget();
        if (m && m->objectName() == QLatin1String("stencilConfirmModal")) {
          for (QPushButton* b : m->findChildren<QPushButton*>())
            if (b->text() == QLatin1String("OK")) { b->click(); return; }
        }
        pumpFor(5);
      }
    });
    removeBtn->click();  // confirm → scatterRows + removeRequested; the dialog stays open
    check(dlg.isVisible(), "the dialog stays open through a batch remove");

    // The animation precondition: one overlay per VISIBLE checked row, each clipped
    // inside the list viewport — never dropped onto the dialog chrome.
    const auto overlays =
        dlg.findChildren<QWidget*>(QString::fromLatin1(DisintegrateOverlay::kObjectName));
    check(overlays.size() == expectedOverlays,
          "one DisintegrateOverlay per visible checked row (scrolled-out rows spawn none)");
    bool inside = !overlays.isEmpty();
    for (QWidget* fx : overlays)
      if (!viewInDlg.contains(fx->geometry())) inside = false;
    check(inside, "every overlay sits inside the list's visible region");

    // Every checked id was resolved to its row: the whole set is retired in place
    // (blanked + unselectable while the dust falls — the dialog no longer closes).
    int liveRows = 0;
    for (int i = 0; i < list->count(); ++i)
      if (!list->item(i)->data(Qt::UserRole).isNull() &&
          list->item(i)->flags() != Qt::NoItemFlags)
        ++liveRows;
    check(liveRows == 0, "every checked row was found and retired on remove");
    check(dlg.action() == ProjectsDialog::Action::None && removed.size() == 14,
          "the remove was signalled (not accept()ed) with all 14 checked ids");
    dlg.reject();
  }

  // ── A press on the checkbox strip SELECTS — it must never open the row ──
  {
    std::printf("checkbox press selects, never opens:\n");
    ProjectsDialog dlg(locals, 5000);
    dlg.show();
    pumpFor(50);
    auto* list = dlg.findChild<QListWidget*>("projectsList");
    check(list != nullptr, "finds the projects list");
    if (!list) return 1;
    QListWidgetItem* it = rowById(list, "l1", false);
    check(it != nullptr, "finds the local row");
    if (!it) { dlg.reject(); return 1; }
    const QRect vr = list->visualItemRect(it);
    const QPointF pos(vr.left() + 12, vr.center().y());
    const QPointF gpos(list->viewport()->mapToGlobal(pos.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, pos, gpos, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, gpos, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &release);
    // Wait out the single-click open timer: pre-fix this raised the open
    // confirm (and closed the dialog); now the dialog must simply stay.
    pumpFor(QApplication::doubleClickInterval() + 120);
    check(dlg.isVisible(), "checkbox-strip click never opens the row (dialog stays up)");
    dlg.reject();
  }

  // ── Filter transitions: a row the SEARCH excludes fades and collapses out (the light
  // support/filterFade motion), never with the scatter a removal uses, and comes back
  // the same way round. Local-only dialog: no server poll to re-list rows mid-flight.
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
    check(dlg.findChild<QWidget*>(DisintegrateOverlay::kObjectName) == nullptr,
          "…with none of the destructive scatter a removal uses");
    // The whole effect belongs to what is LEFT: the matching row keeps its slot (nothing
    // may jump under the pointer) and re-forms out of the filter's own, lighter sand,
    // named apart so nothing counting removals mistakes the two (support/filterFade.hpp).
    check(rowH(keep()) == fullH, "the matching row keeps its slot — no jump under the cursor");
    check(dlg.findChild<QWidget*>(QString::fromLatin1(kFilterDustObjectName)) != nullptr,
          "…and arrives out of the filter's own dust");
    pumpUntil([&] { return dlg.findChild<QWidget*>(
                        QString::fromLatin1(kFilterDustObjectName)) == nullptr; });
    check(!keep()->isHidden() && rowH(keep()) == fullH, "…landing whole, where it always was");

    search->clear();
    check(!drop()->isHidden(), "a revealed row is in the view at once");
    check(filteredIn(drop()), "…and already counted in the filtered set");
    pumpUntil([&] { return rowH(drop()) == fullH; });
    check(rowH(drop()) == fullH, "…and its slot opens to full height");

    // Rapid edits: the LAST needle decides, with nothing left stuck part-collapsed.
    for (const char* q : {"al", "be", "ga", "a", ""}) {
      search->setText(QString::fromLatin1(q));
      pumpFor(kFilterFadeMs / 6);   // each edit interrupts the transition before it
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

  // ── Regression: a live re-list landing mid dust-flight must not crash. setProjects()
  // (what a server poll / MainWindow refresh calls) does list_->clear() — if it lands in
  // the gap between a filter arrival's dust settling and its veil-lift timer firing, the
  // pending timer must not touch the now-deleted QListWidgetItem it started with.
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
    check(dlg.findChild<QWidget*>(QString::fromLatin1(kFilterDustObjectName)) != nullptr,
          "dust is in flight for the arriving row");
    dlg.setProjects(locals);   // the live re-list — clears and rebuilds every row mid-flight
    // Pump well past the veil-lift timer (kFilterDustMs * kFilterDustVeilStop) without
    // crashing — that's the whole regression.
    pumpFor(400);
    check(dlg.isVisible(), "the dialog survives a re-list landing mid dust-flight");
    check(rowById(list, "l1", false) != nullptr, "the rebuilt list still has its rows");
    dlg.reject();
  }

  // ── The footer row never overlaps itself (support/modalChrome addModalFooter) ──
  // The hint sits left and the create/danger buttons right, on one row. Every dialog sets
  // its own minimum size, which stops the layout from raising that minimum to what the row
  // needs — so a wider system font handed the row negative space and drew the hint under
  // the first button. Two legal shapes now: beside the buttons, or — once the row can no
  // longer hold the hint's floor — on its own line above them. Never on top of them.
  // Checked at the app font and at a much wider one.
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

  std::printf(failures ? "FAILED (%d failures)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}

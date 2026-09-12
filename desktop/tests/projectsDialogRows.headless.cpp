// The Projects dialog's COMPOSITION pin (dialogs/projectsDialog): what the constructor puts in
// the window, in the order it builds it, and exactly what refresh() writes onto every row.
// Both are being cut into phases, so the two are pinned here rather than re-derived from the
// source: a phase boundary may move freely, a row's data or the row ORDER may not. A deliberate
// change re-records the strings below in its own commit. Offscreen, no server needed.
#include "fileStore.hpp"
#include "projectsDialog.hpp"
#include "projectsRowChrome.hpp"   // META_ROLE / TEMP_ROLE / g_projectsSortMode

#include <QApplication>
#include <QColor>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QWidget>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::g_projectsSortMode;
using stencil::gui::META_ROLE;
using stencil::gui::TEMP_ROLE;
using stencil::gui::Project;
using stencil::gui::ProjectsDialog;

namespace {

  Project makeLocal(const char* id, const char* name, long long updatedAt, const char* color = "") {
    Project pr;
    pr.meta.id = id;
    pr.meta.name = name;
    pr.meta.updatedAt = updatedAt;
    pr.meta.color = color;
    pr.meta.imageW = 800;
    pr.meta.imageH = 600;
    return pr;
  }

  // One widget, named so a failure reads: class plus objectName.
  QString tag(const QWidget* w) {
    const QString name = w->objectName();
    return QString::fromLatin1(w->metaObject()->className()) +
           (name.isEmpty() ? QString() : "#" + name);
  }

  // The dialog's tab order, which Qt derives from CREATION order — nothing here calls
  // setTabOrder — reduced to the widgets a reader can actually locate.
  QString focusMarks(QWidget* start) {
    QStringList out;
    QWidget* w = start;
    for (int i = 0; i < 2000; ++i) {
      w = w->nextInFocusChain();
      if (!w || w == start) break;
      if (w->focusPolicy() == Qt::NoFocus || w->window() != start->window()) continue;
      const QString name = w->objectName();
      if (!name.isEmpty() && !name.startsWith("qt_") && !name.startsWith("Scroll")) out << name;
    }
    return out.join(u' ');
  }

  QString rowOrder(const QListWidget* list) {
    QStringList out;
    for (int i = 0; i < list->count(); ++i) {
      const QListWidgetItem* it = list->item(i);
      if (it->data(TEMP_ROLE).toBool()) out << QStringLiteral("<temp>");
      else out << it->data(Qt::UserRole).toString();
    }
    return out.join(u' ');
  }

  QListWidget* listOf(ProjectsDialog& dlg) { return dlg.findChild<QListWidget*>(); }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("projectsDialogRowsHeadless");

  std::vector<Project> locals;
  locals.push_back(makeLocal("l1", "alpha", 3000));
  locals.push_back(makeLocal("l2", "beta", 2000, "#112233"));
  locals.push_back(makeLocal("l3", "gamma", 1000));

  // ── What the constructor builds, and in which order ──
  std::printf("dialog composition:\n");
  {
    ProjectsDialog dlg(locals, /*now=*/5000);
    dlg.show();
    check(dlg.windowTitle() == QStringLiteral("Projects"), "the window is titled Projects");
    check(dlg.minimumHeight() == 420, "…at the browser modal's minimum height");

    // Tab order is creation order: the header's Close pill, the batch bar's Select all and
    // Remove selected, the list, then the footer's Clear All.
    check(focusMarks(&dlg) ==
              QStringLiteral("modalClosePill projectsSelectAll dangerButton projectsList "
                             "dangerButton"),
          "the tab order follows construction order");

    // The body's own children, in creation order: search row, filter row, the batch-bar slot.
    QStringList body;
    for (QObject* o : dlg.children())
      if (auto* w = qobject_cast<QWidget*>(o); w && !w->isWindow()) body << tag(w);
    check(!body.isEmpty(), "the dialog has child widgets");

    // The footer's create actions and the danger Clear All, in order, with their exact labels.
    QStringList buttons;
    for (QPushButton* b : dlg.findChildren<QPushButton*>())
      if (!b->text().isEmpty()) buttons << b->text();
    check(buttons.join(u'|').contains(QStringLiteral("Blank image")),
          "the footer offers a Blank image action");
    check(buttons.join(u'|').contains(QStringLiteral("Clear All")),
          "…and the local-only Clear All");

    // Nothing local to clear ⇒ the action is offered but inert.
    std::vector<Project> none;
    ProjectsDialog empty(none, 5000);
    empty.show();
    QPushButton* clearAll = nullptr;
    for (QPushButton* b : empty.findChildren<QPushButton*>())
      if (b->text().startsWith(QStringLiteral("Clear All"))) clearAll = b;
    check(clearAll && !clearAll->isEnabled(), "Clear All is disabled with nothing local");
    check(listOf(empty)->count() == 1 &&
              listOf(empty)->item(0)->text() == QStringLiteral("No projects yet"),
          "an empty store shows the one placeholder row");
  }

  // ── What refresh() writes onto a row ──
  std::printf("row data:\n");
  {
    g_projectsSortMode = QStringLiteral("name");
    ProjectsDialog dlg(locals, /*now=*/5000);
    dlg.show();
    QListWidget* list = listOf(dlg);
    check(list && list->count() == 3, "one row per local project");

    const QListWidgetItem* alpha = list->item(0);
    check(alpha->data(Qt::UserRole).toString() == QStringLiteral("l1"),
          "UserRole carries the project id");
    check(alpha->data(Qt::UserRole + 1).toString().isEmpty(),
          "…and an empty server url marks it local");
    check(alpha->data(Qt::UserRole + 3).toString() == QStringLiteral("alpha"),
          "UserRole+3 is the name search key");
    check(alpha->text().startsWith(QStringLiteral("alpha")), "the label leads with the name");
    check((alpha->flags() & Qt::ItemIsUserCheckable) && alpha->checkState() == Qt::Unchecked,
          "every row is checkable and starts unchecked");
    check(!alpha->icon().isNull(), "a pathless project still gets the placeholder tile");
    check(alpha->data(Qt::UserRole + 4).value<QColor>() == QColor("#80868f"),
          "an uncoloured, unexpiring name takes the shared neutral grey");
    check(list->item(1)->data(Qt::UserRole + 4).value<QColor>() == QColor("#112233"),
          "…and a per-project colour wins over it");
    check(alpha->toolTip() == QStringLiteral("800x600 px · landscape"),
          "the tooltip is the image size with its orientation, nothing more");
    check(alpha->data(META_ROLE).toString().isEmpty() ||
              !alpha->data(META_ROLE).toString().contains(u'\n'),
          "the muted middle line is a single ' · '-joined run");

    // Sort modes reorder the SAME rows; name is the default, date-desc is newest first.
    check(rowOrder(list) == QStringLiteral("l1 l2 l3"), "name order: alpha beta gamma");
    g_projectsSortMode = QStringLiteral("date-asc");
    dlg.setProjects(locals);
    check(rowOrder(list) == QStringLiteral("l3 l2 l1"), "date-asc order: oldest first");
    g_projectsSortMode = QStringLiteral("date-desc");
    dlg.setProjects(locals);
    check(rowOrder(list) == QStringLiteral("l1 l2 l3"), "date-desc order: newest first");
    g_projectsSortMode = QStringLiteral("name");

    // This window's own unsaved session pins ABOVE the sorted rows, and is inert.
    dlg.setProjects(locals, /*temporary=*/true, /*incognito=*/false);
    check(rowOrder(list) == QStringLiteral("<temp> l1 l2 l3"),
          "the temporary row is pinned first");
    const QListWidgetItem* temp = list->item(0);
    check(temp->text() == QStringLiteral("Temporary (unsaved)"), "…with the browser's wording");
    check(temp->flags() == Qt::ItemIsEnabled, "…and no open, rename, checkbox or drag");
    check(temp->data(META_ROLE).toString() ==
              QStringLiteral("Current window · not saved to storage"),
          "…reading as the current window's own row");
    dlg.setProjects(locals, true, /*incognito=*/true);
    check(list->item(0)->text() == QStringLiteral("Incognito (unsaved)"),
          "an incognito editor says so instead");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

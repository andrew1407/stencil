// The project's name as the window wears it (app/project/ProjectTitleController), over stand-in
// hooks: the title and the name field for no project, incognito, a local project, a server link
// and a bare picture; the actions a saved project enables; and a rename in place, refused, taken
// and cancelled.
#include "ProjectTitleController.hpp"
#include "CanvasWidget.hpp"
#include "ProjectNameBar.hpp"
#include "RemoteSession.hpp"
#include "RemoteState.hpp"
#include "WindowActions.hpp"
#include "fileStore.hpp"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QLineEdit>
#include <QScrollArea>
#include <QToolButton>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::gui;

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;
  auto* canvas = new CanvasWidget(&host);
  auto* scroll = new QScrollArea(&host);
  bool incognito = false;
  QString activeProjectId;
  ProjectNameBar nameBar;
  nameBar.field = new QLineEdit(&host);
  nameBar.edit = new QToolButton(&host);
  nameBar.accept = new QToolButton(&host);
  nameBar.cancel = new QToolButton(&host);
  nameBar.colorBtn = new QToolButton(&host);
  WindowActions acts;
  for (QAction** a : {&acts.projectColor, &acts.projectColorClear, &acts.description, &acts.keywords, &acts.links})
    *a = new QAction(&host);
  RemoteState remote;
  remote.session = new RemoteSession(&host, nullptr);
  const QPointer<Notifications> notify;
  const Settings settings;

  QString stored = QStringLiteral("Alpha");
  QString color;
  QStringList renames;
  int infoChanges = 0;
  ProjectTitleController title(
      &host, canvas, scroll, incognito, activeProjectId, nameBar, acts, remote, notify, settings,
      ProjectTitleController::Hooks{
          [&stored] { return stored; },
          [](const QString& name, const QString&) {
            return name == QLatin1String("Taken")
                       ? ProjectTitleController::NameCheck{false, QStringLiteral("That name is taken")}
                       : ProjectTitleController::NameCheck{};
          },
          [&](const QString& id, const QString& name) {
            renames << id + '=' + name;
            stored = name.trimmed();
            return true;
          },
          [&color] { return color; },
          [] { return QString(); },
          [](QToolButton*, const QColor&) {},
          [] { return false; },
          [&infoChanges] { ++infoChanges; },
      });

  title.updateProjectTitle();
  check(host.windowTitle() == QLatin1String("Stencil"), "no project, no picture: the bare title");
  check(!nameBar.field->isEnabled() && nameBar.field->placeholderText() == QLatin1String("No project"),
        "…and a read-only, empty name field");
  check(!acts.projectColor->isEnabled() && !acts.description->isEnabled(), "…and no project actions");
  check(infoChanges == 1, "every update refreshes the image readout");

  incognito = true;
  title.updateProjectTitle();
  check(host.windowTitle() == QString::fromUtf8("Incognito — Stencil"), "incognito names the window so");
  check(!nameBar.field->isEnabled() && nameBar.field->placeholderText() == QLatin1String("Incognito (unsaved)"),
        "…with nothing to rename");

  incognito = false;
  activeProjectId = QStringLiteral("p1");
  title.updateProjectTitle();
  check(host.windowTitle() == QString::fromUtf8("Alpha — Stencil"), "a local project names the window");
  check(nameBar.field->isEnabled() && nameBar.field->isReadOnly() && nameBar.field->text() == QLatin1String("Alpha"),
        "…and fills a renameable, read-only field");
  check(acts.description->isEnabled() && acts.keywords->isEnabled() && acts.links->isEnabled(),
        "a saved project enables its description, keywords and links");
  check(acts.projectColor->isEnabled() && !acts.projectColorClear->isEnabled(),
        "a colour can be picked, and with none there is nothing to clear");
  color = QStringLiteral("#ff0000");
  title.updateProjectTitle();
  check(acts.projectColorClear->isEnabled(), "a custom colour can be cleared");

  title.enterNameEdit();
  check(nameBar.editing && !nameBar.field->isReadOnly(), "rename mode makes the field editable");
  nameBar.field->setText(QStringLiteral("Taken"));
  title.refreshProjectNameButtons();
  check(!nameBar.accept->isEnabled() && nameBar.accept->toolTip() == QLatin1String("That name is taken"),
        "a refused name disables ✓ and says why");
  nameBar.field->setText(QStringLiteral("Beta"));
  title.refreshProjectNameButtons();
  check(nameBar.accept->isEnabled() && nameBar.accept->toolTip().isEmpty(), "a new, valid name enables ✓");
  title.commitProjectName();
  check(renames == QStringList{QStringLiteral("p1=Beta")}, "committing renames the active local project");
  check(!nameBar.editing && nameBar.field->isReadOnly(), "…and leaves rename mode");
  check(host.windowTitle() == QString::fromUtf8("Beta — Stencil"), "…under the stored name");

  title.enterNameEdit();
  nameBar.field->setText(QStringLiteral("Gamma"));
  title.cancelProjectName();
  check(!nameBar.editing && nameBar.field->text() == QLatin1String("Beta") && renames.size() == 1,
        "cancelling puts the stored name back and renames nothing");

  activeProjectId.clear();
  remote.session->getLink().bind(QStringLiteral("http://127.0.0.1:1"), QStringLiteral("s1"),
                                 QStringLiteral("Shared"), QString(), 3);
  title.updateProjectTitle();
  check(host.windowTitle() == QString::fromUtf8("Shared — Stencil"), "a server link names the window");
  check(nameBar.field->isEnabled() && scroll->property("remoteEditing").toBool(),
        "…is renameable, and frames the canvas as a server session");
  check(!acts.description->isEnabled(), "…but is not a saved local project");

  remote.session->getLink().unbind();
  QImage page(40, 30, QImage::Format_RGB32);
  page.fill(Qt::white);
  canvas->loadFromImage(page);
  title.updateProjectTitle();
  check(host.windowTitle() == QString::fromUtf8("image — Stencil"), "an unsaved picture lends its name");
  check(!nameBar.field->isEnabled() && !scroll->property("remoteEditing").toBool(),
        "…read-only, and the server frame is gone");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

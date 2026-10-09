// Headless check of the projects list's order (src/dialogs/projects/list/projectSort.hpp, the twin of
// browser js/ui/projects/window/projectSort.js) and the store behind "Recently opened"
// (io/fileStore.hpp markProjectOpened, the twin of js/core/project/meta/projectOpened.js).
#include "fileStore.hpp"
#include "projectSort.hpp"
#include "projectsRowChrome.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../../support/check.hpp"

using namespace stencil::gui;

namespace {

  QString keys(const std::vector<ProjectSortItem>& items) {
    QStringList out;
    for (const auto& it : items) out << it.key;
    return out.join(QLatin1Char(','));
  }

  // name, opened, edited
  std::vector<ProjectSortItem> sample() {
    return {
        {false, 0, "a", "alpha", 100, 3000},
        {false, 1, "b", "bravo", 300, 0},
        {false, 2, "c", "charlie", 200, 9000},
        {false, 3, "d", "delta", 400, 0},
        {true, 0, "s", "sierra", 500, 0},
    };
  }

  std::vector<ProjectSortItem> sorted(const QString& mode, const QStringList& order = {}) {
    auto items = sample();
    sortProjectItems(items, mode, order);
    return items;
  }

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  std::printf("modes:\n");
  QStringList modes, labels;
  for (const auto& [mode, label] : projectSortModes()) { modes << mode; labels << label; }
  check(modes.join(',') == "opened-desc,opened-asc,name,local,server,date-desc,date-asc,manual",
        "the browser's SORT_MODES, in its order");
  check(labels.first() == "Recently opened" && labels[5] == "Recently edited" && labels[6] == "Least recently edited",
        "the opened and edited labels");
  check(PROJECT_SORT_DEFAULT == "opened-desc" && g_projectsSortMode == PROJECT_SORT_DEFAULT,
        "Recently opened is the default");

  std::printf("order:\n");
  check(keys(sorted("opened-desc")) == "c,a,b,d,s", "recently opened first; never opened last, by name");
  check(keys(sorted("opened-asc")) == "b,d,s,a,c", "least recently opened: the never-opened lead, by name");
  check(keys(sorted("date-desc")) == "s,d,b,c,a", "recently edited by the last save");
  check(keys(sorted("date-asc")) == "a,c,b,d,s", "least recently edited");
  check(keys(sorted("name")) == "a,b,c,d,s", "name");
  check(keys(sorted("server")) == "s,a,b,c,d" && keys(sorted("local")) == "a,b,c,d,s", "server / local first");
  check(keys(sorted("manual", {"d", "a"})) == "d,a,b,c,s", "manual, the unplaced after it by name");
  check(keys(sorted("bogus")) == "a,b,c,d,s", "an unknown mode sorts by name");

  std::printf("opened store:\n");
  QFile::remove(fileStore::projectOpenedPath());
  check(fileStore::loadProjectOpened().isEmpty(), "nothing opened yet");
  fileStore::markProjectOpened("p1", 1000);
  fileStore::markProjectOpened("p2", 2000);
  fileStore::markProjectOpened("p1", 3000);
  fileStore::markProjectOpened(QString(), 4000);
  const QHash<QString, qint64> got = fileStore::loadProjectOpened();
  check(got.size() == 2 && got.value("p1") == 3000 && got.value("p2") == 2000,
        "a re-open restamps; an empty id is ignored");
  check(got.value("never", 0) == 0, "a project not in it reads as never opened");
  for (int i = 0; i < fileStore::PROJECT_OPENED_CAP + 5; ++i)
    fileStore::markProjectOpened(QStringLiteral("n%1").arg(i), 10000 + i);
  const QHash<QString, qint64> capped = fileStore::loadProjectOpened();
  check(capped.size() == fileStore::PROJECT_OPENED_CAP, "capped at the newest 500");
  check(!capped.contains("p1") && !capped.contains("n0") && capped.contains("n504"),
        "…the oldest dropped first");
  check(!QFile::exists(fileStore::projectOpenedPath()), "a stamp is held in memory, its write deferred");
  fileStore::flushWrites();
  QFile persisted(fileStore::projectOpenedPath());
  check(persisted.open(QIODevice::ReadOnly) &&
            QJsonDocument::fromJson(persisted.readAll()).object().size() == fileStore::PROJECT_OPENED_CAP,
        "…and the flushed file holds the capped map");
  persisted.close();
  QFile::remove(fileStore::projectOpenedPath());

  std::printf("projectSort: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}

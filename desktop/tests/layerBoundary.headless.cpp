// Import-direction lint for desktop/src. The layer order (ARCHITECTURE.md) is model → controllers →
// net/, io/ → support/ → canvas/, dialogs/, llm/ → app/, so this reads every #include in the tree and
// fails on the directions that cross back: nothing below app/ includes an app/ header, dialogs/ may
// not include canvas/, and a core/ header may only be reached from model/ — everything still above
// that seam is a frozen allowance list the lint refuses to grow. No file includes one header twice,
// and no header passes MainWindow.hpp or (outside canvas/) CanvasWidget.hpp on. Text only.
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdio>

#include "support/check.hpp"

namespace {

  // Up from the test binary until CLAUDE.md, so the paths read the same from any build dir.
  QDir repoRoot() {
    QDir d(QCoreApplication::applicationDirPath());
    while (!d.exists(QStringLiteral("CLAUDE.md")) && d.cdUp()) {}
    return d;
  }

  // Source files that may include a core/ header until Wave 3 introduces model/. Paths
  // are relative to desktop/src. Shrink this list; never add to it.
  const char* CORE_INCLUDE_ALLOWANCE[] = {
      "app/chat/planTarget/ChatPlanTarget.cpp",         "app/setup/WindowAssemblySignals.cpp",
      "app/selection/SelectedLineBar.hpp",        "app/selection/SelectionPanel.hpp",
      "canvas/draw/strokeGrowth.hpp",
      "llm/plan/opPlan.hpp",
      "app/MainWindow.hpp",             "app/open/MainWindowBlank.cpp",
      "app/view/EditorViewFullscreen.cpp",
      "app/mainWindowHelpers.hpp",      "app/meta/HoverTip.cpp",
      "app/view/MainWindowZoom.cpp",         "app/project/ProjectTransferController.hpp",
      "canvas/input/CanvasDrag.cpp",          "canvas/draw/CanvasDrawClick.cpp",
      "canvas/input/CanvasHold.cpp",          "canvas/input/CanvasHover.cpp",
      "canvas/draw/CanvasLineEdit.cpp",      "canvas/input/CanvasRelease.cpp",
      "canvas/draw/CanvasSelection.cpp",     "canvas/paint/CanvasTransform.cpp",
      "dialogs/crop/CropDialog.hpp",         "dialogs/meta/ExpirationDialog.cpp",
      "dialogs/openImage/preview/OpenImageDialogCropStage.cpp",
      "dialogs/projects/ProjectsDialog.hpp",
      "dialogs/projects/row/projectsRowChrome.hpp",  "io/fileStore.cpp",
      "io/fileStore.hpp",
      "llm/plan/executor/planExecutorParts.hpp",           "llm/plan/executor/planExecutor.hpp",
      "support/theme/cssColor.hpp",           "support/guiHelpers.cpp",
  };

  // One app/ header a sibling still reaches for: the shared name-chip metrics (NAME_CHIP_BOX /
  // NAME_CHIP_GLYPH) that the projects list draws its rows with. They belong in support/.
  const char* APP_INCLUDE_ALLOWANCE[] = {
      "dialogs/projects/ProjectsDialog.cpp:mainWindowHelpers.hpp",
  };

  // Recursive: a group's headers now sit in feature folders under it, and the group still owns them.
  QStringList headersIn(const QDir& dir) {
    QStringList out;
    if (!dir.exists()) return out;
    QDirIterator it(dir.absolutePath(), {QStringLiteral("*.hpp"), QStringLiteral("*.h")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QFileInfo(it.next()).fileName();
    return out;
  }

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QDir root = repoRoot();
  check(root.exists(QStringLiteral("CLAUDE.md")), "repo root found from the binary path");
  const QDir src(root.filePath(QStringLiteral("desktop/src")));
  check(src.exists(), "desktop/src found");

  // Which group owns each header basename, and every core/ header name.
  QHash<QString, QString> owner;   // basename → group dir ("app", "canvas", …)
  for (const QFileInfo& g : src.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    for (const QString& h : headersIn(QDir(g.absoluteFilePath()))) owner.insert(h, g.fileName());
  check(owner.value(QStringLiteral("MainWindow.hpp")) == QStringLiteral("app"),
        "the header index found app/MainWindow.hpp");
  check(owner.value(QStringLiteral("CanvasWidget.hpp")) == QStringLiteral("canvas"),
        "…and canvas/CanvasWidget.hpp");

  QSet<QString> coreHeaders;
  QDirIterator ci(root.filePath(QStringLiteral("core")), {QStringLiteral("*.hpp"), QStringLiteral("*.h")},
                  QDir::Files, QDirIterator::Subdirectories);
  while (ci.hasNext()) {
    const QFileInfo fi(ci.next());
    // build trees and the vendored doctest header are not part of the core API.
    if (fi.absoluteFilePath().contains(QStringLiteral("/build")) ||
        fi.absoluteFilePath().contains(QStringLiteral("/third_party")))
      continue;
    coreHeaders.insert(fi.fileName());
  }
  check(coreHeaders.contains(QStringLiteral("pointMath.hpp")) &&
            coreHeaders.contains(QStringLiteral("ProjectsStore.hpp")),
        "the core header index is populated");

  QSet<QString> coreAllowed, appAllowed;
  for (const char* p : CORE_INCLUDE_ALLOWANCE) coreAllowed.insert(QString::fromLatin1(p));
  for (const char* p : APP_INCLUDE_ALLOWANCE) appAllowed.insert(QString::fromLatin1(p));

  // A quoted include, whether written bare ("x.hpp") or with a group prefix ("../app/x.hpp").
  static const QRegularExpression INCLUDE(QStringLiteral("^\\s*#\\s*include\\s+\"([^\"]+)\""));

  // Any include, quoted or angled; a quoted one is the same header under every path spelling.
  static const QRegularExpression ANY_INCLUDE(QStringLiteral("^\\s*#\\s*include\\s+([<\"])([^>\"]+)[>\"]"));
  static const QRegularExpression CONDITIONAL(QStringLiteral("^\\s*#\\s*(if|ifdef|ifndef|endif)\\b"));
  QStringList intoApp, intoCanvas, intoCore, staleCore, staleApp, twice, passedOn;
  QSet<QString> sawCore, sawApp;
  int scanned = 0;
  QDirIterator it(src.absolutePath(),
                  {QStringLiteral("*.cpp"), QStringLiteral("*.hpp"), QStringLiteral("*.mm")},
                  QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QString rel = src.relativeFilePath(path);
    const QString group = rel.section('/', 0, 0);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) continue;
    ++scanned;
    QSet<QString> included;
    int depth = 0;   // an include repeated across #if branches is not a duplicate
    for (const QByteArray& line : f.readAll().split('\n')) {
      const QString text = QString::fromUtf8(line);
      if (const auto c = CONDITIONAL.match(text); c.hasMatch())
        depth += c.captured(1) == QStringLiteral("endif") ? -1 : 1;
      if (const auto any = ANY_INCLUDE.match(text); any.hasMatch() && depth == 0) {
        const QString key = any.captured(1) == QStringLiteral("\"")
                                ? QFileInfo(any.captured(2)).fileName() : any.captured(2);
        if (included.contains(key)) twice << (rel + " → " + key);
        included.insert(key);
      }
      const auto m = INCLUDE.match(text);
      if (!m.hasMatch()) continue;
      const QString inc = QFileInfo(m.captured(1)).fileName();
      // A header that includes the window or the canvas hands it to every file that includes it.
      if (rel.endsWith(QStringLiteral(".hpp")) &&
          (inc == QStringLiteral("MainWindow.hpp") ||
           (inc == QStringLiteral("CanvasWidget.hpp") && group != QStringLiteral("canvas"))))
        passedOn << (rel + " → " + inc);

      if (coreHeaders.contains(inc) && !owner.contains(inc)) {
        // model/ IS the seam: it may include core/ freely, and nothing above it may.
        if (rel.startsWith(QStringLiteral("model/"))) continue;
        sawCore.insert(rel);
        if (!coreAllowed.contains(rel)) intoCore << (rel + " → core/" + inc);
        continue;
      }
      const QString from = owner.value(inc);
      if (from.isEmpty() || from == group) continue;
      if (from == QStringLiteral("app")) {
        const QString key = rel + ":" + inc;
        sawApp.insert(key);
        if (!appAllowed.contains(key)) intoApp << (rel + " → app/" + inc);
      }
      if (from == QStringLiteral("canvas") && group == QStringLiteral("dialogs"))
        intoCanvas << (rel + " → canvas/" + inc);
    }
  }
  check(scanned > 150, "the whole desktop/src tree was read");

  // ── Rule 1: app/ is the top of the order; nothing below it may reach up.
  for (const QString& v : intoApp) std::printf("  app-include: %s\n", qPrintable(v));
  check(intoApp.isEmpty(), "no file below app/ includes an app/ header");

  // ── Rule 2: dialogs/ and canvas/ are siblings.
  for (const QString& v : intoCanvas) std::printf("  canvas-include: %s\n", qPrintable(v));
  check(intoCanvas.isEmpty(), "no dialogs/ file includes a canvas/ header");

  // ── Rule 3: core/ enters the GUI through one seam only.
  for (const QString& v : intoCore) std::printf("  core-include: %s\n", qPrintable(v));
  check(intoCore.isEmpty(), "no unlisted file includes a core/ header");

  // ── Rule 4: one include per header per file.
  for (const QString& v : twice) std::printf("  included twice: %s\n", qPrintable(v));
  check(twice.isEmpty(), "no file includes the same header twice");

  // ── Rule 5: the window and the canvas are a translation unit's include, never a header's —
  // a header names them by forward declaration, so its includers do not all recompile with them.
  for (const QString& v : passedOn) std::printf("  passed on by a header: %s\n", qPrintable(v));
  check(passedOn.isEmpty(), "no header includes MainWindow.hpp, nor CanvasWidget.hpp outside canvas/");

  // ── The allowances are a ratchet, so a stale entry is a failure too: it means the
  // violation is gone and the list should have shrunk with it.
  for (const QString& a : coreAllowed)
    if (!sawCore.contains(a)) staleCore << a;
  for (const QString& a : appAllowed)
    if (!sawApp.contains(a)) staleApp << a;
  for (const QString& v : staleCore) std::printf("  stale core allowance: %s\n", qPrintable(v));
  for (const QString& v : staleApp) std::printf("  stale app allowance: %s\n", qPrintable(v));
  check(staleCore.isEmpty(), "every core-include allowance is still needed");
  check(staleApp.isEmpty(), "every app-include allowance is still needed");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

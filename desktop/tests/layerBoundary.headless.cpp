// Import-direction lint for desktop/src. The layer order (ARCHITECTURE.md,
// .claude/rules/architecture.md) is: model → controllers → net/, io/ → support/ →
// canvas/, dialogs/, llm/ → app/. A layer may use everything to its left and nothing to
// its right, so this reads every #include in the tree and fails on the directions that
// cross back. Text only; it compiles no app source and needs no display.
//
// Three rules, from the Phase 6 plan:
//   1. nothing below app/ may include an app/ header (app/ is the top of the order);
//   2. dialogs/ may not include a canvas/ header (they are siblings, not a stack);
//   3. a core/ header may only be included from model/ — the seam Wave 3's DocumentModel
//      will own, which the .stc engine opened (model/ScriptDoc). Everything still above
//      the seam is a frozen allowance list below: the lint refuses NEW ones, and each
//      entry is deleted as the wave moves that call site behind the model.
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
      "app/ChatPlanTarget.cpp",         "app/MainWindow.cpp",
      "app/SelectedLineBar.hpp",        "app/SelectionPanel.hpp",
      "canvas/chainEdit.hpp",           "canvas/strokeGrowth.hpp",
      "llm/opPlan.hpp",
      "app/MainWindow.hpp",             "app/MainWindowBlank.cpp",
      "app/MainWindowChat.cpp",         "app/MainWindowFullscreenZoom.cpp",
      "app/mainWindowHelpers.hpp",      "app/MainWindowHoverDetail.cpp",
      "app/MainWindowLaunchImage.cpp",  "app/MainWindowProjectLoad.cpp",
      "app/MainWindowZoom.cpp",         "app/ProjectTransferController.hpp",
      "canvas/CanvasDrag.cpp",          "canvas/CanvasDrawClick.cpp",
      "canvas/CanvasHold.cpp",          "canvas/CanvasHover.cpp",
      "canvas/CanvasLineEdit.cpp",      "canvas/CanvasRelease.cpp",
      "canvas/CanvasSelection.cpp",     "canvas/CanvasSettings.cpp",
      "canvas/CanvasTransform.cpp",     "canvas/CanvasWidget.hpp",
      "dialogs/CropDialog.hpp",         "dialogs/ExpirationDialog.cpp",
      "dialogs/ProjectsDialog.cpp",     "dialogs/ProjectsDialog.hpp",
      "dialogs/projectsRowChrome.hpp",  "io/fileStore.cpp",
      "io/fileStore.hpp",               "llm/opPlan.cpp",
      "llm/planExecutor.cpp",           "llm/planExecutor.hpp",
      "support/cssColor.hpp",           "support/guiHelpers.cpp",
  };

  // One app/ header a sibling still reaches for: the shared name-chip metrics
  // (NAME_CHIP_BOX / NAME_CHIP_GLYPH) that the projects list draws its rows with. They
  // belong in support/; moving them is its own commit.
  const char* APP_INCLUDE_ALLOWANCE[] = {
      "dialogs/ProjectsDialog.cpp:mainWindowHelpers.hpp",
  };

  QStringList headersIn(const QDir& dir) {
    QStringList out;
    if (!dir.exists()) return out;
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.hpp"), QStringLiteral("*.h")},
                                                 QDir::Files))
      out << fi.fileName();
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

  QStringList intoApp, intoCanvas, intoCore, staleCore, staleApp;
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
    for (const QByteArray& line : f.readAll().split('\n')) {
      const auto m = INCLUDE.match(QString::fromUtf8(line));
      if (!m.hasMatch()) continue;
      const QString inc = QFileInfo(m.captured(1)).fileName();

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

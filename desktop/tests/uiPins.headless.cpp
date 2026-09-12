// Appearance pins for the desktop app — a regression net for refactors that must not
// change a single rendered pixel. Two independent pins:
//   1. the app stylesheet (theme.cpp buildStylesheet) hashed for every dark × accent
//      combination into tests/pins/stylesheets.txt — proof that moving the QSS out to a
//      .qss resource kept it byte-exact;
//   2. twelve rendered states — eight off one real MainWindow, four standalone dialogs —
//      grabbed at devicePixelRatio 1 AND 2 and diffed against tests/pins/<platform>/*.png,
//      with a small tolerance for antialiasing noise.
// Baselines are per platform (fonts and native metrics differ) — a platform with none
// SKIPS the image half rather than failing. Rewrite them with STENCIL_UPDATE_UI_PINS=1.
// Offscreen; STENCIL_NO_ANIM=1 keeps every reveal off so the grabs are settled.
#include "fileStore.hpp"
#include "theme.hpp"
#include "uiPins.states.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStringList>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <cstdio>

using namespace stencil::gui;

#include "support/uiPin.hpp"

namespace {

  void pinStylesheets() {
    std::printf("the app stylesheet, per theme and accent:\n");
    QStringList lines;
    for (const bool dark : {false, true})
      for (const AccentPreset& a : accentPresets()) {
        const QByteArray qss = buildStylesheet(dark, a.key).toUtf8();
        const QString sum = QString::fromLatin1(
            QCryptographicHash::hash(qss, QCryptographicHash::Sha256).toHex());
        lines << QStringLiteral("%1|%2  %3").arg(dark ? "dark" : "light", a.key, sum);
      }
    check(!accentPresets().empty(), "accents.json gave the accent presets");
    const QString path = pinsDir() + "/stylesheets.txt";
    const QByteArray now = (lines.join('\n') + "\n").toUtf8();
    if (updating()) {
      QDir().mkpath(pinsDir());
      QFile f(path);
      check(f.open(QIODevice::WriteOnly) && f.write(now) == now.size(), "wrote the hashes");
      return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      check(false, "stylesheets.txt is readable");
      std::printf("      missing %s (rewrite with STENCIL_UPDATE_UI_PINS=1)\n", qPrintable(path));
      return;
    }
    const QStringList was = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    check(was == lines, "every dark x accent stylesheet hashes as it did");
    for (int i = 0; i < qMin(was.size(), lines.size()); ++i)
      if (was[i] != lines[i]) std::printf("      %s -> %s\n", qPrintable(was[i]), qPrintable(lines[i]));
    if (was.size() != lines.size())
      std::printf("      %lld pinned rows, %lld now\n", qint64(was.size()), qint64(lines.size()));
  }

}  // namespace

int main(int argc, char** argv) {
  // A private state dir per run: a MainWindow reads its theme/accent from settings.json,
  // and a leftover one from an earlier run would repaint every pin.
  QTemporaryDir state;
  qputenv("STENCIL_STATE_DIR", state.path().toUtf8());

  QApplication app(argc, argv);
  QApplication::setStyle(QStyleFactory::create("Fusion"));   // main.cpp forces this app-wide
  qApp->setQuitOnLastWindowClosed(false);

  // The shipped defaults, minus everything whose rendering is not ours to decide:
  Settings settings;
  settings.themeMode = QStringLiteral("light");   // "system" would follow the host's scheme
  settings.accentColor = QStringLiteral("violet");
  settings.autosave = false;                      // no "Saved" toast drifting into a grab
  settings.llmBaseUrl = QStringLiteral("http://127.0.0.1:1");   // the chat gear's dot, always off
  fileStore::saveSettings(settings);

  pinStylesheets();

  const bool haveBaselines = updating() || QDir(shotsDir()).exists();
  if (!haveBaselines) {
    std::printf("\nSKIP: no %s baselines under %s — rewrite them there with "
                "STENCIL_UPDATE_UI_PINS=1\n", PLATFORM, qPrintable(pinsDir()));
  } else {
    QImage img(240, 160, QImage::Format_RGB32);
    img.fill(Qt::white);
    const QString png = QDir(state.path()).filePath("input.png");
    img.save(png, "PNG");

    std::printf("\nthe window's states, at dpr %d:\n", qRound(qApp->devicePixelRatio()));
    pinWindowStates(png);
    std::printf("\nthe dialogs, at dpr %d:\n", qRound(qApp->devicePixelRatio()));
    pinDialogs(png);
    check(shotCount == 12, "every pinned state was reached");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

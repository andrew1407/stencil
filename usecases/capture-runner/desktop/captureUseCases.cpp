// Entry point of the desktop use-case capture: a private state dir seeded with settings and
// three projects, then the shots this pass was asked for (STENCIL_DOCS_THEME +
// STENCIL_DOCS_SHOTS). Built by desktop/cmake/StencilTests.cmake behind
// STENCIL_DOCS_CAPTURE=ON; run through usecases/capture-runner/desktop.mjs, which reads
// config/desktop.json, sets the env once per theme and assembles the GIF.
#include "captureShared.hpp"

#include "fileStore.hpp"
#include "connectionStore.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <cstdio>

using namespace stencil::gui;

namespace {
  Settings pinned(const QString& theme) {
    Settings s;
    s.themeMode = theme;
    s.accentColor = QStringLiteral("violet");
    s.autosave = false;
    s.nativeMenuBar = false;                             // drawn in-window, so the grab shows it
    s.llmProvider = QStringLiteral("ollama");
    s.llmBaseUrl = QStringLiteral("http://127.0.0.1:1");  // the status dot stays off
    // A collaboration server with an LLM proxy: the chat then talks to the real model.
    const QString server = qEnvironmentVariable("STENCIL_DOCS_SERVER_URL");
    const QString token = qEnvironmentVariable("STENCIL_DOCS_SERVER_TOKEN");
    if (!server.isEmpty() && !token.isEmpty()) {
      s.llmProvider = QStringLiteral("stencil-server");
      s.llmServerUrl = server;
      stencil::net::connectionStore::saveServers({{server, token, QStringLiteral("session")}});
    }
    return s;
  }

  // Three saved projects with real images, so the Projects dialog has rows and thumbnails.
  void seedProjects(const QString& stateDir) {
    std::vector<Project> projects;
    const struct { const char* id; const char* name; QColor fill; } rows[] = {
      {"p1", "kitchen plan", Qt::white}, {"p2", "night sketch", Qt::black}, {"p3", "poster draft", QColor("#fde68a")}};
    long long when = QDateTime::currentMSecsSinceEpoch();
    for (const auto& r : rows) {
      QImage img(960, 640, QImage::Format_RGB32);
      img.fill(r.fill);
      const QString path = QDir(stateDir).filePath(QString::fromLatin1(r.id) + ".png");
      img.save(path, "PNG");
      Project p;
      p.meta.id = r.id;
      p.meta.name = r.name;
      p.meta.createdAt = p.meta.updatedAt = when;
      p.imagePath = path;
      projects.push_back(p);
      when -= 3'600'000;
    }
    fileStore::saveProjects(projects);
    fileStore::flushWrites();
  }
}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir state;
  qputenv("STENCIL_STATE_DIR", state.path().toUtf8());
  if (outDir().isEmpty() || framesDir().isEmpty()) {
    std::fprintf(stderr, "set STENCIL_DOCS_OUT and STENCIL_DOCS_FRAMES (usecases/capture-runner/desktop.mjs does)\n");
    return 2;
  }
  QDir().mkpath(outDir());
  QDir().mkpath(framesDir());

  QApplication app(argc, argv);
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  qApp->setQuitOnLastWindowClosed(false);

  // STENCIL_DOCS_MODE=clip films the theme wipe; anything else takes the stills this pass
  // was named (empty = all of them).
  const bool clip = qEnvironmentVariable("STENCIL_DOCS_MODE") == QLatin1String("clip");
  const QString theme = qEnvironmentVariable("STENCIL_DOCS_THEME", QStringLiteral("dark"));
  fileStore::saveSettings(pinned(theme));
  seedProjects(state.path());
  std::printf("%s:\n", qPrintable(theme));
  if (clip) MainWindowGuiTest::themeClip();
  else MainWindowGuiTest::windowStates(theme, ShotSet(qEnvironmentVariable("STENCIL_DOCS_SHOTS")));
  std::printf("done\n");
  return 0;
}

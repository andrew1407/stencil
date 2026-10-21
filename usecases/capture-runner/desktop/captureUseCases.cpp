// Entry point of the desktop use-case capture: a private state dir seeded with settings and two
// projects, then the shots this pass was asked for (STENCIL_DOCS_THEME + STENCIL_DOCS_SHOTS).
// Built by desktop/cmake/tests/gui.cmake behind STENCIL_DOCS_CAPTURE=ON; run through
// usecases/capture-runner/desktop.mjs, which reads config/desktop.json, sets the env once per
// theme and assembles the GIFs.
#include "captureStrip.hpp"

#include "fileStore.hpp"
#include "connectionStore.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QSettings>
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
    // A provider set up the way a reader would set one; the chat shots answer through the canned
    // plan behind the real client (captureAssistant.cpp).
    s.llmProvider = QStringLiteral("ollama");
    s.llmBaseUrl = QStringLiteral("http://localhost:11434");
    s.llmModel = QStringLiteral("llama3.2-vision");
    // A collaboration server is saved, and dialled at start only when its LLM proxy answers the chat;
    // otherwise serverShots connects last, so the local shots list local projects only.
    const QString server = qEnvironmentVariable("STENCIL_DOCS_SERVER_URL");
    const QString token = qEnvironmentVariable("STENCIL_DOCS_SERVER_TOKEN");
    const bool saved = !server.isEmpty() && !token.isEmpty();
    const bool real = saved && qEnvironmentVariable("STENCIL_DOCS_REAL_LLM") == QLatin1String("1");
    stencil::net::connectionStore::saveServers(
        saved ? QVector<stencil::net::SavedServer>{{server, token, QStringLiteral("session")}}
              : QVector<stencil::net::SavedServer>{});
    stencil::net::connectionStore::setAutoConnect(real);
    if (real) {
      s.llmProvider = QStringLiteral("stencil-server");
      s.llmServerUrl = server;
    }
    return s;
  }

  // The browser's ochre-on-cream wash, painted pixel by pixel (Qt has no filter for it).
  QImage sepia(QImage img) {
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
      for (int x = 0; x < img.width(); ++x) {
        const int r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
        row[x] = qRgba(qMin(255, int(r * .393 + g * .769 + b * .189)), qMin(255, int(r * .349 + g * .686 + b * .168)),
                       qMin(255, int(r * .272 + g * .534 + b * .131)), qAlpha(row[x]));
      }
    }
    return img;
  }

  // Two older projects with real pictures, named as the browser walkthrough names its own; the
  // third, "Kitchen plan", is the one the dialog shots open (captureDialogs.cpp).
  void seedProjects(const QString& stateDir) {
    QImage survey(960, 640, QImage::Format_RGB32);
    survey.fill(QColor("#dde3ea"));
    const QImage icon(envOr("STENCIL_DOCS_ICON_FILE", ""));
    const struct { const char* id; const char* name; QImage img; stencil::core::Lines lines; } rows[] = {
      {"p1", "Poster draft", icon.isNull() ? scene() : sepia(icon), {}},
      {"p2", "Floor survey", survey,
       {line({{160, 120}, {800, 120}, {800, 520}, {160, 520}, {160, 120}}, "#2563eb", 6, "solid")}},
    };
    std::vector<Project> projects;
    long long when = QDateTime::currentMSecsSinceEpoch() - 3'600'000;
    for (const auto& r : rows) {
      const QString path = QDir(stateDir).filePath(QString::fromLatin1(r.id) + ".png");
      r.img.save(path, "PNG");
      Project p;
      p.meta.id = r.id;
      p.meta.name = r.name;
      p.meta.createdAt = p.meta.updatedAt = when;
      p.meta.expiresAt = when + 7LL * 24 * 3'600'000;   // a week, the app's default period
      p.imagePath = path;
      p.lines = r.lines;
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
  // Preferences as INI files in the state dir: the saved servers and switches go with it.
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, state.path());
  QCoreApplication::setOrganizationName(QStringLiteral("StencilDocsCapture"));
  QCoreApplication::setApplicationName(QStringLiteral("stencilDocsCapture"));
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

// The window states behind usecases/docs/desktop/img — the ui-pins states plus a canned
// assistant turn, the blank fills, an image from a URL and the theme-swap frames.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "OpenImageDialog.hpp"
#include "../../../desktop/src/llm/LlmClient.hpp"

#include <QCheckBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QToolButton>
#include <algorithm>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  // The URLs, prompts and canned plan come from config/shared.json through desktop.mjs; the
  // literals below are the same values, for a run started by hand.
  QString envOr(const char* key, const char* fallback) {
    const QString value = qEnvironmentVariable(key);
    return value.isEmpty() ? QString::fromUtf8(fallback) : value;
  }

  const QString FAVICON_URL = envOr("STENCIL_DOCS_FAVICON_URL",
      "https://raw.githubusercontent.com/andrew1407/stencil/main/browser/favicon.svg");
  const QString ICON_URL = envOr("STENCIL_DOCS_ICON_URL",
      "https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png");
  const QString PROMPT = envOr("STENCIL_DOCS_PROMPT",
      "Warm it up with sepia, outline the centre and show me two more takes");
  const QString REAL_PROMPT = envOr("STENCIL_DOCS_REAL_PROMPT",
      "Frame the S with a dashed red rectangle and warm the picture up with sepia");
  // The clip the video shots open, and the same clip over http. desktop.mjs makes it with
  // ffmpeg into the scratch dir and serves it, so no binary is committed; empty = skip.
  const QString CLIP_FILE = qEnvironmentVariable("STENCIL_DOCS_CLIP");
  const QString CLIP_URL = qEnvironmentVariable("STENCIL_DOCS_CLIP_URL");

  const QString PLAN = envOr("STENCIL_DOCS_PLAN", R"({"version":1,"reply":"Applied a sepia tint and framed the centre with a dashed outline. Two variants: one rotated a quarter turn, one in black and white.","actions":[{"op":"filter","mode":"sepia"},{"op":"layout","lines":[{"points":[{"x":96,"y":64},{"x":864,"y":64},{"x":864,"y":576},{"x":96,"y":576},{"x":96,"y":64}],"color":"#1e63c8","thickness":3,"pointSize":4,"style":"dashed","locked":false,"fillColor":"transparent"}]}],"variants":[{"label":"rotated","actions":[{"op":"rotate","dir":"right"}]},{"label":"black & white","actions":[{"op":"filter","mode":"bw"}]}]})");

  // Canned ollama-shaped reply, the seam LlmClient.headless.cpp and the chat GUI suites use.
  struct CannedTransport : stencil::llm::LlmTransport {
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&, const QByteArray&,
                  std::function<void(int, QByteArray, QString)> cb) override {
      const QJsonObject wire{{"message", QJsonObject{{"content", PLAN}}}};
      cb(200, QJsonDocument(wire).toJson(QJsonDocument::Compact), QString());
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)> cb) override {
      cb(200, QByteArrayLiteral("{}"), QString());
    }
  };

  // The turn is over when a second card body (the reply) exists and no card is the pending "…".
  bool replied(QWidget* dock) {
    int bodies = 0;
    bool pending = false;
    for (QLabel* l : dock->findChildren<QLabel*>()) {
      const QString b = l->property("chatBody").toString();
      if (b.isEmpty()) continue;
      if (b == QStringLiteral("…")) pending = true; else ++bodies;
    }
    return bodies >= 2 && !pending;
  }

  QString suffixed(const QString& name, const QString& theme) { return name + "-" + theme; }

  stencil::core::Line line(std::vector<stencil::core::Point> pts, const char* color, double thick, const char* style) {
    stencil::core::Line l;
    l.points = std::move(pts);
    l.color = color;
    l.thickness = thick;
    l.style = style;
    return l;
  }


}  // namespace

// The Open Image dialog with a VIDEO in it: taken mid-clip, so the scrub bar's accent fill
// reads as a position rather than an empty rail. Browser twin: browser/videoSteps.mjs.
void MainWindowGuiTest::grabVideoDialog(MainWindow& win, const QString& name,
                                      const std::function<void(OpenImageDialog*)>& load,
                                      bool crop) {
  bool done = false;
  QTimer::singleShot(0, [&] {
    auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
    if (!dlg) return;
    load(dlg);
    // Offscreen has no GPU decoder on every host, so a clip that never arrives is a skip.
    if (!waitUntil([dlg] { return !dlg->previewedImage().isNull() && dlg->isVideo(); }, 20000)) {
      std::printf("  %s SKIPPED (the clip did not decode)\n", qPrintable(name));
      done = true;
      dlg->reject();
      return;
    }
    if (dlg->frameSlider_) dlg->frameSlider_->setValue(dlg->frameSlider_->maximum() / 2);
    waitUntil([] { return false; }, 600);   // let the seek land on the player
    if (crop && dlg->cropPage_) {
      dlg->cropPage_->setChecked(true);
      waitUntil([] { return false; }, 700);   // the box eases in over the picture
    }
    pumpFor(400);
    saveOver(name, &win, dlg);
    done = true;
    dlg->reject();
  });
  QTimer::singleShot(30000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
  win.openImage();
  waitUntil([&] { return done; }, 32000);
  pumpFor(200);
}

void MainWindowGuiTest::windowStates(const QString& theme, const ShotSet& shots) {
  static CannedTransport canned;
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(1280, 860);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) return;
  pumpFor(250);
  auto* canvas = win.findChild<CanvasWidget*>();
  const QString editorEmpty = suffixed("editor-empty", theme);
  if (shots.has(editorEmpty)) save(editorEmpty, &win);

  const auto fit = [&] { pumpFor(150); win.actFit_->trigger(); pumpFor(150); clearToasts(&win); };
  win.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  fit();
  if (shots.has("blank-white")) save("blank-white", &win);

  const QString linesSelection = suffixed("lines-selection", theme);
  if (shots.has(linesSelection)) {
    canvas->setLines({line({{120, 110}, {520, 260}, {300, 520}}, "#c81e1e", 4, "solid"),
                      line({{620, 140}, {840, 560}}, "#1e63c8", 3, "dashed")});
    canvas->selectLineByIndex(0);
    pumpFor(150);
    save(linesSelection, &win);
    canvas->setLines({});
  }

  if (shots.has("blank-black")) {
    win.createBlankImage(Qt::black, 960, 640);
    fit();
    save("blank-black", &win);
    win.createBlankImage(Qt::white, 960, 640);
    fit();
  }

  if (shots.has("open-from-url")) {
    const QImage before = canvas->image();
    win.openSourceHere(FAVICON_URL, 0, false);
    if (waitUntil([&] { return canvas->hasImage() && canvas->image().size() != before.size(); }, 12000)) {
      fit();
      save("open-from-url", &win);
    } else {
      std::printf("  open-from-url SKIPPED (the URL did not load)\n");
    }
    win.createBlankImage(Qt::white, 960, 640);
    fit();
  }

  // The canvas context menu, grabbed from inside its own popup loop.
  if (shots.has("context-menu")) {
    auto* area = win.findChild<QScrollArea*>();
    bool opened = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      waitUntil([&menu] { return (menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())); }, 2000);
      if (!menu) return;
      opened = true;
      pumpFor(200);
      saveOver("context-menu", &win, menu);
      menu->close();
    });
    QTimer::singleShot(3000, [] { if (QWidget* stuck = QApplication::activePopupWidget()) stuck->close(); });
    if (area) QTest::mouseClick(area->viewport(), Qt::RightButton, {}, QPoint(40, 40));
    waitUntil([&] { return opened; }, 2500);
    pumpFor(100);
  }

  // The assistant: the real model when the state dir carries a server (see main), else a
  // canned plan through the real client — either way the canvas really changes.
  const QString assistantDocked = suffixed("assistant-docked", theme);
  if (shots.hasAny({assistantDocked, QStringLiteral("assistant-floating")})) {
    const bool real = win.settings_.llmProvider == QLatin1String("stencil-server");
    if (real) {
      win.openSourceHere(ICON_URL, 0, false);
      waitUntil([canvas] { return canvas->hasImage() && canvas->image().width() == 512; }, 15000);
      fit();
    } else {
      win.settings_.llmProvider = "ollama";
      win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&canned);
    }
    ChatDock* chat = win.chatDock_;
    win.addDockWidget(Qt::RightDockWidgetArea, chat, Qt::Horizontal);
    chat->setFloating(false);
    win.actChat_->setChecked(true);
    waitUntil([chat] { return chat->isVisible(); });
    auto* input = chat->findChild<QPlainTextEdit*>("chatInput");
    if (input) {
      input->setPlainText(real ? REAL_PROMPT : PROMPT);
      QTest::keyClick(input, Qt::Key_Return);
      if (!waitUntil([chat] { return replied(chat); }, real ? 150000 : 10000)) {
        std::printf("  assistant: no reply —");
        for (QLabel* l : chat->findChildren<QLabel*>())
          if (!l->property("chatNote").toString().isEmpty()) std::printf(" [%s]", qPrintable(l->property("chatNote").toString()));
        std::printf("\n");
      }
      pumpFor(400);
      fit();
      if (shots.has(assistantDocked)) save(assistantDocked, &win);
      if (shots.has("assistant-floating")) {
        chat->setFloating(true);
        chat->resize(420, 560);
        pumpFor(200);
        save("assistant-floating", &win);
        chat->setFloating(false);
      }
    }
    win.actChat_->setChecked(false);
    pumpFor(100);
  }

  // The meta windows are gated on a SAVED project: give it keywords and no description, and keep
  // the lines on the canvas — every window dims and blurs what it covers (support/ModalBackdrop).
  canvas->setLines({line({{140, 150}, {520, 260}, {330, 520}}, "#c81e1e", 4, "solid"),
                    line({{610, 170}, {860, 540}}, "#1e63c8", 3, "dashed"),
                    line({{200, 560}, {780, 600}}, "#2e9e4f", 3, "dotted")});
  {
    win.projectList_.erase(std::remove_if(win.projectList_.begin(), win.projectList_.end(),
                                          [](const stencil::gui::Project& p) {
                                            return p.meta.id == "usecases-doc";
                                          }),
                           win.projectList_.end());
    stencil::gui::Project pr;
    pr.meta.id = "usecases-doc";
    pr.meta.name = "Kitchen plan";
    // Keywords, but NO description: the description window is photographed empty, showing
    // the prompt that says what to type there.
    pr.meta.keywords = {"kitchen", "floor plan", "survey", "draft", "north wall"};
    win.projectList_.push_back(pr);
    win.activeProjectId_ = "usecases-doc";
    win.refreshActions();
  }
  fit();

  const struct { const char* action; QString name; } dialogs[] = {
    {"Visuals & Settings…", suffixed("settings-dialog", theme)},
    {"Projects…", suffixed("projects-dialog", theme)},
    {"Open Image…", QStringLiteral("open-image-dialog")},
    {"Crop Image…", QStringLiteral("crop-dialog")},
    {"Servers…", QStringLiteral("connect-dialog")},
    {"Keyboard Shortcuts…", QStringLiteral("shortcuts-dialog")},
    {"Stencil Script…", QStringLiteral("script-dialog")},
    {"AI Assistant Settings…", QStringLiteral("assistant-settings-dialog")},
    {"Controls & Shortcuts Info", QStringLiteral("help-dialog")},
    {"Project Description…", QStringLiteral("description-dialog")},
    {"Project Keywords…", QStringLiteral("keywords-dialog")},
    {"Image Links…", QStringLiteral("links-dialog")},
    {"Open In…", QStringLiteral("open-in-dialog")},
  };
  for (const auto& dialog : dialogs)
    if (shots.has(dialog.name)) grabModal(win, actionNamed(win, QString::fromUtf8(dialog.action)), dialog.name);
  // The clip off disk, the clip from a link, and the crop box over the player.
  const QStringList videoShots = {"open-video-local", "open-video-url", "crop-video"};
  if (shots.hasAny(videoShots) && CLIP_FILE.isEmpty())
    std::printf("  video shots SKIPPED (STENCIL_DOCS_CLIP unset)\n");
  else if (shots.hasAny(videoShots)) {
    // The tail of OpenImageDialog::browse(), which is what a picked file runs.
    const auto fromFile = [](OpenImageDialog* dlg) {
      dlg->path_->setText(CLIP_FILE);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
    };
    const auto fromUrl = [](OpenImageDialog* dlg) {
      dlg->tabs_->setCurrentIndex(1);   // URL link
      QTest::keyClicks(dlg->url_, CLIP_URL);   // setText alone never fires textEdited,
      waitUntil([dlg] { return dlg->previewBtn_->isEnabled(); }, 2000);   // so Preview stays off
      dlg->previewBtn_->click();
    };
    if (shots.has("open-video-local")) grabVideoDialog(win, "open-video-local", fromFile, false);
    // A URL clip goes straight to QMediaPlayer::setSource, whose platform media stack refuses the
    // capture's own little server — this shot wants a real web server.
    if (shots.has("open-video-url") && !CLIP_URL.isEmpty())
      grabVideoDialog(win, "open-video-url", fromUrl, false);
    if (shots.has("crop-video")) grabVideoDialog(win, "crop-video", fromFile, true);
  }

  if (shots.has("accent-picker"))
    grabModal(win, win.findChild<QAction*>(QStringLiteral("actAccent")), "accent-picker");
}

// The theme wipe (SWAP_MS + the dust's life) at ~30 fps: its own pass, at 1x and with
// motion on, because a 2x grab outlasts a frame.
void MainWindowGuiTest::themeClip() {
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(1280, 860);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) return;
  pumpFor(250);
  auto* canvas = win.findChild<CanvasWidget*>();
  win.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  pumpFor(150);
  win.actFit_->trigger();
  canvas->setLines({line({{120, 110}, {520, 260}, {300, 520}}, "#c81e1e", 4, "solid"),
                    line({{620, 140}, {840, 560}}, "#1e63c8", 3, "dashed")});
  pumpFor(300);
  clearToasts(&win);
  win.toggleTheme();
  film("theme-swap", &win, 1100, 33);
}

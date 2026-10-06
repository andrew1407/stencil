#include "MainWindow.hpp"
#include "SourceOpener.hpp"
#include "ChatSessionController.hpp"
#include "Notifications.hpp"
#include "mainWindowHelpers.hpp"
#include "StencilFileSync.hpp"

// Loading a source by path or URL, and the .stencil project file.

namespace stencil::gui {

  // Open a portable .stencil project; mirrors browser DrawingApp.applyProjectFile. The picture
  // decodes on the pool; `done` hears whether it landed (a newer load wins). `incognito` files no
  // project and links no file, so nothing it holds is written back.
  void SourceOpener::openProjectFile(const QString& path, std::function<void(bool)> done,
                                     bool incognito) {
    QByteArray bytes;
    if (!readFileBytes(path, bytes)) {
      w.notify->error("Could not read the project file");
      if (done) done(false);
      return;
    }
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(bytes, pf, &err)) {
      w.notify->error("Invalid .stencil file: " + err);
      if (done) done(false);
      return;
    }
    w.decodeForCanvas(
        [image = pf.imageBytes] { return QImage::fromData(image); },
        [this, path, bytes, pf, done, incognito](const QImage& img) {
          if (img.isNull()) {
            w.notify->error("Could not decode the project image");
            if (done) done(false);
            return;
          }
          w.activeProjectId.clear();   // an opened project file is a fresh editor (Save to Project keeps it)
          w.loadImageWithLayout(img, pf.layout, pf.imageBytes, pf.imageExt);
          w.docSource.currentSource = pf.source;
          w.docSource.currentResource = pf.resource;
          // Only a file that carried a theme changes the user's; a custom-hex accent is ignored (desktop
          // uses presets).
          if (pf.hasTheme) {
            bool changed = false;
            if (pf.themeMode == "light" || pf.themeMode == "dark") {
              w.settings.themeMode = pf.themeMode;
              changed = true;
            }
            if (!pf.themeAccent.isEmpty()) {
              for (const auto& preset : accentPresets()) {
                if (pf.themeAccent == preset.key) {
                  w.settings.accentColor = pf.themeAccent;
                  changed = true;
                  break;
                }
              }
            }
            if (changed) {
              w.applyTheme();
              fileStore::saveSettings(w.settings);
            }
          }
          if (!incognito) {
            w.createLocalProject(pf.name, /*announce=*/false, /*fromFile=*/true);
            w.stencilSync->link(path, bytes);
          }
          // Chat persistence (§12.3): adopt the file's saved chat when the opt-in is on.
          if (w.settings.saveChatsWithProject) {
            w.chatSession->restoreChatFromDoc(pf.chat);
            if (!pf.chat.isEmpty()) {
              if (Project* pr = w.findProject(w.activeProjectId.toStdString())) {
                pr->chat = w.chatSession->buildActiveChatDoc();
                fileStore::saveProjects(w.projectList);
              }
            }
          }
          w.fitToWindow();
          w.playImageArrival();   // a .stencil open is a fresh image landing (browser: ghostIn)
          if (done) done(true);
        },
        [done] { if (done) done(false); });
  }

  void SourceOpener::openProjectFileHere(const QString& path, bool incognito) {
    freshEditorIn(incognito);
    openProjectFile(path, {}, incognito);
  }
}  // namespace stencil::gui

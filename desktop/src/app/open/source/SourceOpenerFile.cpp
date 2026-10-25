#include "MediaLoader.hpp"
#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "SourceOpener.hpp"
#include "ChatSessionController.hpp"
#include "Notifications.hpp"
#include "fileBytes.hpp"
#include "StencilFileSync.hpp"

#include <memory>

// Loading a source by path or URL, and the .stencil project file.

namespace stencil::gui {

  // Open a portable .stencil project; mirrors browser DrawingApp.applyProjectFile. The picture
  // decodes on the pool; `done` hears whether it landed (a newer load wins). `incognito` files no
  // project and links no file, so nothing it holds is written back.
  void SourceOpener::openProjectFile(const QString& path, std::function<void(bool)> done,
                                     bool incognito) {
    // Filled on the pool (the read, the parse and its base64, the picture), read here once it lands.
    struct Parsed {
      QByteArray bytes;
      fileStore::ProjectFileData pf;
      QString err;
    };
    const auto parsed = std::make_shared<Parsed>();
    w.decodeForCanvas(
        [path, parsed] {
          QString why;
          if (!readFileBytes(path, parsed->bytes)) parsed->err = QStringLiteral("Could not read the project file");
          else if (!fileStore::parseProjectFile(parsed->bytes, parsed->pf, &why))
            parsed->err = QStringLiteral("Invalid .stencil file: ") + why;
          if (!parsed->err.isEmpty()) return QImage();
          bool tooLarge = false;
          const QImage img = decodeCapped(parsed->pf.imageBytes, &tooLarge);
          if (tooLarge) parsed->err = imageTooLargeMessage();
          else if (img.isNull()) parsed->err = QStringLiteral("Could not decode the project image");
          return img;
        },
        [this, path, parsed, done, incognito](const QImage& img) {
          if (img.isNull()) {
            w.notify->error(parsed->err);
            if (done) done(false);
            return;
          }
          const QByteArray& bytes = parsed->bytes;
          const fileStore::ProjectFileData& pf = parsed->pf;
          w.projectHost.activeProjectId.clear();   // an opened project file is a fresh editor (Save to Project keeps it)
          w.loadImageWithLayout(img, pf.layout, pf.imageBytes, pf.imageExt);
          w.projectHost.docSource.currentSource = pf.source;
          w.projectHost.docSource.currentResource = pf.resource;
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
              w.persistSettings();
            }
          }
          if (!incognito) {
            w.projectHost.createLocalProject(pf.name, /*announce=*/false, /*fromFile=*/true);
            w.stencilSync->link(path, bytes);
          }
          // Chat persistence (§12.3): adopt the file's saved chat when the opt-in is on.
          if (w.settings.saveChatsWithProject) {
            w.chatSession->restoreChatFromDoc(pf.chat);
            if (!pf.chat.isEmpty()) {
              if (Project* pr = w.projectHost.findProject(w.projectHost.activeProjectId.toStdString())) {
                pr->chat = w.chatSession->buildActiveChatDoc();
                SharedState::instance().saveProjects(&w);
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

#pragma once
#include "fileStore.hpp"
#include <QString>
#include <QStringList>
#include <functional>

class QColor;
class QImage;

namespace stencil::gui {

  class MainWindow;

  // Opens a picture into the editor or a new window: the Open-Image dialog's outcomes, a local
  // file, a URL or video frame through MediaLoader, a blank page, and replacing the active
  // project's image. Filing the result as a project stays on the window.
  class SourceOpener {
   public:
    explicit SourceOpener(MainWindow& w) : w(w) {}

    void openImageDialog(bool startBlank);
    void createBlankImageFromDialog(const QColor& color, int width, int height);
    void createBlankImage(const QColor& color, int width, int height);
    void openImageHere(const QString& path, bool incognito);
    void openImageInNewWindow(const QString& path, bool incognito);
    // `fallbacks`: the ranked candidates to try after `src` (a linked image's other urls).
    void openSourceHere(const QString& src, int frame, bool incognito,
                        const QStringList& fallbacks = {});
    // `cropRect` is what the Open-Image stage was left on; empty ⇒ the new window centres it.
    void openSourceInNewWindow(const QString& src, int frame, bool incognito,
                               const QStringList& fallbacks = {}, bool hasPreview = false,
                               bool cropToPage = false, bool cropAlbum = false,
                               const QString& cropPage = QString(),
                               const core::CropRect& cropRect = {});
    void openPreviewedImageHere(const QImage& image, const QString& localPath,
                                const QString& provSource, bool incognito, bool cropToPage,
                                bool cropAlbum, const QString& cropPage,
                                const core::CropRect& cropRect = {});
    bool canReplaceActive() const;
    void replaceProjectImage(const QString& path, bool rename, bool keepAnnotations);
    void replaceServerOriginal(std::function<void()> done = {});
    void loadLocalImageReset(const QString& path, std::function<void(bool)> done);
    void onLaunchImageLoaded(const QImage& image, const QString& localPath);
    void applyQuickCrop();
    void applyLayoutFromSource(const QString& src);
    void openImageSource(const QString& src, int frame, const QStringList& fallbacks = {});
    void ensureMediaLoader();
    void retainSourceFromFile(const QString& path);

    // Browser linksModal.js.
    void openLinks();
    void loadImageByUrl(const QString& source, const QString& resource, int frame);
    // A plan's opens: `done` answers once the picture (or project) is in, so the plan's next
    // action edits it — at once when it loaded synchronously, else from the event loop.
    void chatOpenFileThen(const QString& path, std::function<void(bool ok, const QString& err)> done);
    void chatLoadSourceThen(const QString& src, bool incognito, int frame,
                            std::function<void(bool ok, const QString& why)> done);
    void openImage();
    void pasteImage();
    void openProjectFile(const QString& path, std::function<void(bool)> done = {});

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui

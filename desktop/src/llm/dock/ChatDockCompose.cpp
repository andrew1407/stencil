// Composer send + the attachment queue behind it.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "MediaLoader.hpp"

#include <QPlainTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QStringList>

namespace stencil::gui {

  using namespace chatdock;
  void ChatDock::submit() {
    if (isBusy()) return;  // single turn at a time; Enter is a no-op while busy
    const QString text = input->toPlainText().trimmed();
    if (text.isEmpty()) return;
    input->clear();
    emit sendRequested(text);
  }

  void ChatDock::onSendClicked() {
    if (isBusy()) {
      emit stopRequested();  // STOP mode
      return;
    }
    submit();
  }

  void ChatDock::pickMedia() {
    // One dialog for both media kinds (browser single-attach parity); each
    // pick routes by suffix — the same sniffers the paste/drop paths use.
    bool overCap = false;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Attach images or videos", QString(),
        "Images & videos (*.png *.jpg *.jpeg *.webp *.gif *.bmp "
        "*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.mpg *.mpeg *.ogv);;"
        "Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;"
        "Videos (*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.mpg *.mpeg *.ogv)");
    for (const QString& p : paths) {
      if (isVideoFileName(p)) {
        cmp.videoPath = p;
        emit videoAttached(p);
      } else {
        if (cmp.images.size() >= MAX_ATTACHMENTS) { overCap = true; continue; }   // §7: three per message
        const QImage img = readImageFile(p);
        if (!img.isNull()) {
          cmp.images.append(img);
          cmp.imageNames.append(QFileInfo(p).fileName());
        }
      }
    }
    refreshAttachmentTray();
    if (overCap) warnAttachmentCap();
  }

  // Browser parity: a queue past the §7 cap is SAID, as an accent toast on the owner's stack, not a
  // transcript card. Once per batch: a drop of five pictures arrives one image at a time.
  void ChatDock::warnAttachmentCap() {
    if (capToastAt.isValid() && capToastAt.elapsed() < 1500) return;
    capToastAt.start();
    emit toastRequested(
        QStringLiteral("Up to %1 images per message — the extra ones were not attached.")
            .arg(MAX_ATTACHMENTS));
  }

  void ChatDock::addAttachmentImage(const QImage& img, const QString& name) {
    if (img.isNull()) return;
    if (cmp.images.size() >= MAX_ATTACHMENTS) { warnAttachmentCap(); return; }
    cmp.images.append(img);
    // Kept in lockstep with cmp.images so a chip can say WHICH picture it holds. Empty where there is
    // nothing to say (a clipboard bitmap has no filename) - the chip falls back to the dimensions.
    cmp.imageNames.append(name);
    refreshAttachmentTray();
  }

  void ChatDock::clearAttachments() {
    cmp.images.clear();
    cmp.imageNames.clear();
    cmp.videoPath.clear();
    refreshAttachmentTray();
  }
}  // namespace stencil::gui

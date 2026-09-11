// Composer send + the attachment queue behind it.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "mediaLoader.hpp"

#include <QPlainTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QStringList>

namespace stencil::gui {

  using namespace chatdock;
  void ChatDock::submit() {
    if (isBusy()) return;  // single turn at a time; Enter is a no-op while busy
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    input_->clear();
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
        videoPath_ = p;
        emit videoAttached(p);
      } else {
        if (images_.size() >= kMaxAttachments) { overCap = true; continue; }   // §7: three per message
        const QImage img = readImageFile(p);
        if (!img.isNull()) {
          images_.append(img);
          imageNames_.append(QFileInfo(p).fileName());
        }
      }
    }
    refreshAttachmentTray();
    if (overCap) warnAttachmentCap();
  }

  // Browser parity: a queue past the §7 cap is SAID, not silently swallowed — as an
  // accent toast on the owner's stack, not a transcript card. Once per batch: a drop
  // of five pictures arrives one image at a time, which would otherwise post it four times.
  void ChatDock::warnAttachmentCap() {
    if (capToastAt_.isValid() && capToastAt_.elapsed() < 1500) return;
    capToastAt_.start();
    emit toastRequested(
        QStringLiteral("Up to %1 images per message — the extra ones were not attached.")
            .arg(kMaxAttachments));
  }

  void ChatDock::addAttachmentImage(const QImage& img, const QString& name) {
    if (img.isNull()) return;
    if (images_.size() >= kMaxAttachments) { warnAttachmentCap(); return; }
    images_.append(img);
    // Kept in lockstep with images_ so a chip can say WHICH picture it holds. Empty
    // where there is nothing to say (a clipboard bitmap has no filename) — the chip
    // falls back to the dimensions there, as it always did.
    imageNames_.append(name);
    refreshAttachmentTray();
  }

  void ChatDock::clearAttachments() {
    images_.clear();
    imageNames_.clear();
    videoPath_.clear();
    refreshAttachmentTray();
  }
}  // namespace stencil::gui

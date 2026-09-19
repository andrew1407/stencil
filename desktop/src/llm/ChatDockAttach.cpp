// Attachment intake: what a drop may carry, the composer's drop cue, mime decode.
// Split out of ChatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "MediaLoader.hpp"

#include <QPlainTextEdit>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QAbstractAnimation>

namespace stencil::gui {

  using namespace chatdock;
  // Can this payload become an attachment? Raw image data (a drag out of a browser)
  // or a local image/video file. Shared by the composer's drag-enter and its drop.
  bool ChatDock::canAttachMime(const QMimeData* mime) const {
    if (!mime) return false;
    if (mime->hasImage()) return true;
    if (mime->hasUrls()) {
      for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        if (isImageFileName(path) || isVideoFileName(path)) return true;
      }
    }
    return false;
  }

  // The dock swallows what the composer didn't take: accepted so the drag can land,
  // then deliberately ignored. Without this the drop reached the main window and
  // offered to open the image as a project — from a gesture aimed at the chat.
  void ChatDock::dragEnterEvent(QDragEnterEvent* event) {
    if (canAttachMime(event->mimeData())) event->acceptProposedAction();
  }

  void ChatDock::dropEvent(QDropEvent* event) {
    // Accepted, not acted on: attaching is the composer's job (eventFilter), and the
    // transcript is for reading. Accepting stops it falling through to the window.
    if (canAttachMime(event->mimeData())) event->acceptProposedAction();
  }
  // The cue over the composer while a drag hovers it (browser .chat-drop-cue): an
  // icon that bobs beside the label. Only the composer takes a drop, so only the
  // composer lights up — a drop over the transcript belongs to the window behind it.
  void ChatDock::showDropCue(bool on) {
    if (!dropCue_) return;
    if (on) {
      // Over the INPUT only. Covering the whole composer swallowed the attachment
      // chips, the busy bar and the send/… buttons under one slab, which is not what
      // the browser does — there the cue sits on the box you type in.
      QRect r = inputArea_->rect();
      if (input_ && input_->isVisible()) {
        const QPoint tl = input_->mapTo(inputArea_, QPoint(0, 0));
        r = QRect(tl, input_->size());
      }
      dropCue_->setGeometry(r);
      dropCue_->raise();
      dropCue_->show();
      if (dropCueAnim_ && dropCueAnim_->state() != QAbstractAnimation::Running)
        dropCueAnim_->start();
    } else {
      dropCue_->hide();
      if (dropCueAnim_) dropCueAnim_->stop();
    }
  }

  bool ChatDock::attachFromMimeData(const QMimeData* mime) {
    if (!mime) return false;
    bool any = false;
    bool overCap = false;
    if (mime->hasUrls()) {
      for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        if (isImageFileName(path)) {
          if (images_.size() >= MAX_ATTACHMENTS) { overCap = true; continue; }   // §7: three per message
          const QImage img = readImageFile(path);
          if (!img.isNull()) {
            images_.append(img);
            imageNames_.append(QFileInfo(path).fileName());
            any = true;
          }
        } else if (isVideoFileName(path)) {
          // Same routing as the attach-video button: MainWindow extracts a
          // preview frame / offers the server upload.
          videoPath_ = path;
          any = true;
          emit videoAttached(path);
        }
      }
    }
    if (!any && mime->hasImage() && images_.size() >= MAX_ATTACHMENTS) overCap = true;
    if (!any && mime->hasImage() && images_.size() < MAX_ATTACHMENTS) {
      const QImage img = qvariant_cast<QImage>(mime->imageData());
      if (!img.isNull()) {
        images_.append(img);
        // A web-drag delivers the bitmap plus its source URL: name the chip from the URL when its last
        // segment is a real filename ("cat.jpg"). An endpoint segment ("…/images?q=…") stays unnamed and
        // the chip shows the dimensions instead (browser fileNameForUrl parity).
        QString name;
        for (const QUrl& u : mime->urls()) {
          if (u.isLocalFile()) continue;
          const QString f = u.fileName();
          if (f.contains(QLatin1Char('.')) && f.size() <= 80) { name = f; break; }
        }
        imageNames_.append(name);
        any = true;
      }
    }
    if (any) refreshAttachmentTray();
    if (overCap) warnAttachmentCap();
    return any;
  }
}  // namespace stencil::gui

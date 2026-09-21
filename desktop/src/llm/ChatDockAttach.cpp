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

  // The dock swallows what the composer did not take: accepted so the drag can land, then ignored.
  // Without this the drop reached the main window and offered to open the image as a project.
  void ChatDock::dragEnterEvent(QDragEnterEvent* event) {
    if (canAttachMime(event->mimeData())) event->acceptProposedAction();
  }

  void ChatDock::dropEvent(QDropEvent* event) {
    // Accepted, not acted on: attaching is the composer's job (eventFilter), and the
    // transcript is for reading. Accepting stops it falling through to the window.
    if (canAttachMime(event->mimeData())) event->acceptProposedAction();
  }
  // The cue over the composer while a drag hovers it (browser .chat-drop-cue). Only the composer
  // takes a drop, so only the composer lights up.
  void ChatDock::showDropCue(bool on) {
    if (!cmp.dropCue) return;
    if (on) {
      // Over the INPUT only: covering the whole composer swallowed the chips, the busy bar and the
      // send buttons under one slab. In the browser the cue sits on the box you type in.
      QRect r = log.inputArea->rect();
      if (input && input->isVisible()) {
        const QPoint tl = input->mapTo(log.inputArea, QPoint(0, 0));
        r = QRect(tl, input->size());
      }
      cmp.dropCue->setGeometry(r);
      cmp.dropCue->raise();
      cmp.dropCue->show();
      if (cmp.dropCueAnim && cmp.dropCueAnim->state() != QAbstractAnimation::Running)
        cmp.dropCueAnim->start();
    } else {
      cmp.dropCue->hide();
      if (cmp.dropCueAnim) cmp.dropCueAnim->stop();
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
          if (cmp.images.size() >= MAX_ATTACHMENTS) { overCap = true; continue; }   // §7: three per message
          const QImage img = readImageFile(path);
          if (!img.isNull()) {
            cmp.images.append(img);
            cmp.imageNames.append(QFileInfo(path).fileName());
            any = true;
          }
        } else if (isVideoFileName(path)) {
          // Same routing as the attach-video button: MainWindow extracts a
          // preview frame / offers the server upload.
          cmp.videoPath = path;
          any = true;
          emit videoAttached(path);
        }
      }
    }
    if (!any && mime->hasImage() && cmp.images.size() >= MAX_ATTACHMENTS) overCap = true;
    if (!any && mime->hasImage() && cmp.images.size() < MAX_ATTACHMENTS) {
      const QImage img = qvariant_cast<QImage>(mime->imageData());
      if (!img.isNull()) {
        cmp.images.append(img);
        // A web-drag delivers the bitmap plus its source URL: name the chip from the URL when its last
        // segment is a real filename. An endpoint segment stays unnamed (browser fileNameForUrl parity).
        QString name;
        for (const QUrl& u : mime->urls()) {
          if (u.isLocalFile()) continue;
          const QString f = u.fileName();
          if (f.contains(QLatin1Char('.')) && f.size() <= 80) { name = f; break; }
        }
        cmp.imageNames.append(name);
        any = true;
      }
    }
    if (any) refreshAttachmentTray();
    if (overCap) warnAttachmentCap();
    return any;
  }
}  // namespace stencil::gui

#pragma once
// What a drag carries, ranked into try order: a local file, or the http(s)/data: candidates for
// one picture. Twin of extractDraggedImageUrls (browser/js/core/pointer/dragImageUrl.js) and of
// the extension's lib/drop/dragUrl.js, which pick the same way.
#include "dragPasteboard.hpp"

#include <QString>
#include <QStringList>

class QMimeData;

namespace stencil::gui {

  struct DropSrc {
    enum Kind { NONE, LOCAL_FILE, URL } kind = NONE;
    QString value;          // path (LocalFile) or the preferred url (Url)
    QStringList fallbacks;  // Url only: the ranked candidates after `value`
  };

  // `bitmap` is the picture the drag source rendered, as a data: url; empty when unencoded.
  // `native` is what the OS pasteboard held beyond QMimeData; an empty one ranks as before.
  QStringList rankedImageUrls(const QMimeData* mime, const QString& bitmap = {},
                              const support::DragPasteboard& native = {});
  DropSrc droppableSource(const QMimeData* mime, const QString& bitmap = {},
                          const support::DragPasteboard& native = {});

  // Whether the drag is worth accepting; a drag-move asks it on every pixel, so it never encodes.
  bool canDrop(const QMimeData* mime);

  // The drag published LINKS ONLY: every candidate is an http(s) url that names no image, and
  // the source handed over no bitmap, promised file or <img> src. Its picture never crossed.
  bool isLinkOnlyDrag(const QMimeData* mime, const QString& bitmap = {},
                      const support::DragPasteboard& native = {});

  // STENCIL_DEBUG_DROP=1 logs what the drag source handed Qt and the OS; inert without it.
  void logDroppedMime(const QMimeData* mime, const support::DragPasteboard& native = {});

}  // namespace stencil::gui

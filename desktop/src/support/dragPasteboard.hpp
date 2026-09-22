#pragma once
// What a native drop carries that Qt never puts on QMimeData: macOS maps a few pasteboard
// flavors and drops the rest, so `public.html` and the PROMISED FILE a browser offers Finder
// are invisible to dropSources. Body: dragPasteboardMac.mm; a no-op everywhere else.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

namespace stencil::support {

  struct DragPasteboard {
    QByteArray html;    // public.html, undecoded: a drag fragment may be UTF-16 with a BOM
    QStringList urls;
    QString filePath;   // bytes the drag source wrote for us; empty when none arrived
  };

  // ms a promised file gets to land before the drop gives up and walks on to the next candidate.
  inline constexpr int PROMISE_BUDGET_MS = 1200;

  // Reads the flavors and, where one is offered, waits out a promised file. Drop only: a
  // promise costs a file write, and a drag-move asks what it carries on every pixel.
  DragPasteboard readDragPasteboard();

  // Where a promise is written: per-user temp, owner-only, emptied on every read.
  QString dropScratchDir();

  // Polls every POLL_MS until `poll` yields a path or `budgetMs` is spent; empty on the deadline.
  QString awaitFile(const std::function<QString()>& poll, int budgetMs);

  // The platform primitives behind readDragPasteboard(), inert off Apple.
  namespace pasteboard {
    struct Offer {
      QByteArray html;
      QStringList urls;
      bool promise = false;
    };
    Offer read();
    bool fetch(const QString& destDir);
    QString poll();
    void abandon();
  }  // namespace pasteboard

}  // namespace stencil::support

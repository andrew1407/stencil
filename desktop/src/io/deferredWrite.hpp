#pragma once
// Atomic, coalesced, off-GUI-thread JSON writes for the file store.
//
// Every persisted file here is rewritten WHOLE on each change (projects.json goes out
// again for a single row's rename), so schedule() merges a burst into one write on the
// thread pool and atomic() replaces the target in one rename — an interrupted write can
// never leave a truncated file behind.
//
// schedule()/flush() are GUI-thread only (they own QTimers); atomic() is callable from
// any thread.
#include <QByteArray>
#include <QString>
#include <functional>

namespace stencil::gui::deferredWrite {

  // Replace `path` with `bytes` via a temp file + rename. `ownerOnly` re-narrows the
  // result to 0600 (settings holds llmApiKey in the clear). False = nothing was written.
  bool atomic(const QString& path, const QByteArray& bytes, bool ownerOnly = false);

  // Write `path` once, `delayMs` after the last call naming it — later calls replace the
  // pending one. `build` produces the bytes ON A POOL THREAD, so it must own everything
  // it reads (capture by value; never touch GUI state).
  void schedule(const QString& path, int delayMs, std::function<QByteArray()> build,
                bool ownerOnly = false);

  // Write everything still pending and wait for the pool to finish — before a read that
  // must see the bytes, and on the way out of the app.
  void flush();

}  // namespace stencil::gui::deferredWrite

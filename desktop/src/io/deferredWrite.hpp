#pragma once
// Coalesced, atomic, off-GUI-thread JSON writes: every file is rewritten WHOLE, so a burst
// becomes one pool write and a rename. schedule()/flush() are GUI-thread only; atomic() any thread.
#include <QByteArray>
#include <QString>
#include <functional>

namespace stencil::gui::deferredWrite {

  // `ownerOnly` re-narrows to 0600 (settings holds the API key in the clear). False = nothing written.
  bool atomic(const QString& path, const QByteArray& bytes, bool ownerOnly = false);

  // `build` runs ON A POOL THREAD: capture by value, never touch GUI state.
  void schedule(const QString& path, int delayMs, std::function<QByteArray()> build,
                bool ownerOnly = false);

  // Flush before a read that must see the bytes, and on the way out of the app.
  void flush();

}  // namespace stencil::gui::deferredWrite

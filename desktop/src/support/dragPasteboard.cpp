#include "dragPasteboard.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

// Portable half of the native drag reader: the scratch it writes into, the bounded wait that
// keeps a promise from hanging the drop, and — off Apple — the primitives as no-ops.

namespace stencil::support {

  namespace {
    constexpr int POLL_MS = 20;
  }

  QString dropScratchDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (base.isEmpty()) return {};
    QDir dir(base + QStringLiteral("/stencil-drop"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return {};
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner);
    // A drop leaves at most one file behind, and the next drop is what takes it away.
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (fi.isDir()) QDir(fi.absoluteFilePath()).removeRecursively();
      else QFile::remove(fi.absoluteFilePath());
    }
    return dir.absolutePath();
  }

  QString awaitFile(const std::function<QString()>& poll, int budgetMs) {
    QElapsedTimer clock;
    clock.start();
    for (;;) {
      const QString path = poll();
      if (!path.isEmpty()) return path;
      if (clock.elapsed() >= budgetMs) return {};
      QThread::msleep(POLL_MS);
    }
  }

  DragPasteboard readDragPasteboard() {
    const pasteboard::Offer offer = pasteboard::read();
    DragPasteboard out;
    out.html = offer.html;
    out.urls = offer.urls;
    if (offer.promise && pasteboard::fetch(dropScratchDir()))
      out.filePath = awaitFile(&pasteboard::poll, PROMISE_BUDGET_MS);
    if (out.filePath.isEmpty()) pasteboard::abandon();
    return out;
  }

#ifndef Q_OS_MACOS
  namespace pasteboard {
    Offer read() { return {}; }
    bool fetch(const QString&) { return false; }
    QString poll() { return {}; }
    void abandon() {}
  }  // namespace pasteboard
#endif

}  // namespace stencil::support

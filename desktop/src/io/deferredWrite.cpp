#include "deferredWrite.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QTimer>
#include <QWaitCondition>

namespace stencil::gui::deferredWrite {

  namespace {
    struct Job {
      QPointer<QTimer> timer;
      std::function<QByteArray()> build;   // null once handed to the pool
      quint64 generation = 0;              // bumped per hand-off, on the GUI thread
    };

    // GUI-thread only (schedule/flush/fire), so it needs no lock of its own.
    QHash<QString, Job>& jobs() {
      static QHash<QString, Job> j;
      return j;
    }

    // One write at a time: a fresh burst can be scheduled while the previous one is still
    // on the pool, and the later bytes have to land last.
    QMutex& writeGate() {
      static QMutex m;
      return m;
    }
    // Under writeGate: the newest generation written per path. QMutex is not FIFO, so an older
    // hand-off reaching the gate after a newer one is skipped rather than written over it.
    QHash<QString, quint64>& written() {
      static QHash<QString, quint64> w;
      return w;
    }
    // Guards `inFlight` and wakes flush() when the pool goes quiet.
    QMutex& countGate() {
      static QMutex m;
      return m;
    }
    QWaitCondition& quiet() {
      static QWaitCondition c;
      return c;
    }
    int inFlight = 0;

    // Hand `path`'s pending bytes to the pool. A no-op when nothing is pending.
    void fire(const QString& path) {
      const auto it = jobs().find(path);
      if (it == jobs().end() || !it->build) return;
      auto build = std::move(it->build);
      const quint64 generation = ++it->generation;
      it->build = nullptr;
      if (it->timer) it->timer->stop();
      {
        QMutexLocker lk(&countGate());
        ++inFlight;
      }
      QThreadPool::globalInstance()->start([path, build, generation] {
        const QByteArray bytes = build();
        {
          QMutexLocker lk(&writeGate());
          quint64& last = written()[path];
          if (generation > last) {
            last = generation;
            atomic(path, bytes);
          }
        }
        QMutexLocker lk(&countGate());
        if (--inFlight == 0) quiet().wakeAll();
      });
    }
  }  // namespace

  bool atomic(const QString& path, const QByteArray& bytes, bool ownerOnly) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    // Narrowed before a byte lands, so the secret is never world-readable in the temp file.
    if (ownerOnly) f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(bytes) != bytes.size()) {
      f.cancelWriting();
      return false;
    }
    if (!f.commit()) return false;
    // Re-applied on every write, so a file from an older build is tightened on save.
    if (ownerOnly)
      QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
  }

  void schedule(const QString& path, int delayMs, std::function<QByteArray()> build) {
    Job& job = jobs()[path];
    job.build = std::move(build);
    if (!job.timer) {
      job.timer = new QTimer(qApp);   // dies with the app; nothing schedules after that
      job.timer->setSingleShot(true);
      QObject::connect(job.timer, &QTimer::timeout, job.timer, [path] { fire(path); });
    }
    job.timer->start(delayMs);   // a later call restarts the window, coalescing the burst
  }

  void flush() {
    for (const QString& path : jobs().keys()) fire(path);
    QMutexLocker lk(&countGate());
    while (inFlight > 0) quiet().wait(&countGate());
  }

}  // namespace stencil::gui::deferredWrite

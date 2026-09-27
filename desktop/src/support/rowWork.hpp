#pragma once
// Spread independent work over the global thread pool. core/ owns no threading policy:
// it exposes half-open row slices of its kernels and leaves the policy to each adapter.
#include <QFutureWatcher>
#include <QObject>
#include <QPromise>
#include <QSemaphore>
#include <QThreadPool>
#include <algorithm>
#include <functional>
#include <memory>

namespace stencil::support {

  // Returns once every slice has finished, so the caller may keep buffers on its own
  // stack. Fewer than `minPer` per slice runs inline.
  inline void forEachSlice(int count, int minPer,
                           const std::function<void(int i0, int i1)>& body) {
    if (count <= 0) return;
    QThreadPool* pool = QThreadPool::globalInstance();
    const int want = std::max(1, count / std::max(1, minPer));
    const int slices = std::min(std::max(1, pool->maxThreadCount()), want);
    if (slices <= 1) { body(0, count); return; }
    const int step = (count + slices - 1) / slices;
    QSemaphore done;
    int handed = 0;
    for (int i = step; i < count; i += step) {
      const int i0 = i, i1 = std::min(count, i + step);
      pool->start([&body, &done, i0, i1] {
        body(i0, i1);
        done.release();
      });
      ++handed;
    }
    body(0, std::min(step, count));
    done.acquire(handed);
  }

  // `work` runs on the pool and must touch no GUI object; `done` runs later on `ctx`'s thread,
  // and never once `ctx` is gone (the watcher dies with it).
  template <typename T>
  void runOnPool(QObject* ctx, std::function<T()> work, std::function<void(T)> done) {
    auto* watcher = new QFutureWatcher<T>(ctx);
    auto promise = std::make_shared<QPromise<T>>();
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher,
                     [watcher, done = std::move(done)] {
                       watcher->deleteLater();
                       if (watcher->future().resultCount() > 0) done(watcher->result());
                     });
    watcher->setFuture(promise->future());
    promise->start();
    QThreadPool::globalInstance()->start([promise, work = std::move(work)] {
      promise->addResult(work());
      promise->finish();
    });
  }

}  // namespace stencil::support

#pragma once
// Spread independent work over the global thread pool. core/ owns no threading policy:
// it exposes half-open row slices of its kernels and leaves the policy to each adapter.
#include <QSemaphore>
#include <QThreadPool>
#include <algorithm>
#include <functional>

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

}  // namespace stencil::support

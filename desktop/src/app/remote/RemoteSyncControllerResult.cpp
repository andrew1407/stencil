// The layout push and the baked result: the push fired off its debounce, the result scheduled
// once edits idle and uploaded under the gap, each flushed and waited for at close. The timers
// and the peer-change path are in RemoteSyncController.cpp.
#include "RemoteSyncController.hpp"
#include "RemoteSession.hpp"
#include <QDateTime>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace stencil::gui {

  void RemoteSyncController::firePush() {
    // Our own result upload bumps the version, and a push in flight owns the link's version; a
    // push racing either would 409 and union-merge against a canvas mid-swap.
    if (resultInFlight || *remotePushing) { pushTimer->start(100); return; }
    pushBurstStart = 0;   // burst flushed — start a fresh max-wait window next edit
    if (session->address().isEmpty()) { pushFinished(); return; }
    h.saveToServer();
  }

  // Edits made while the held push was in flight go out first; the reclose runs off the stack
  // that fired the push, which may be the close event itself.
  void RemoteSyncController::pushFinished() {
    if (!pushSettled) return;
    if (pushTimer->isActive()) { pushTimer->stop(); firePush(); return; }
    QTimer::singleShot(0, this, [fn = std::exchange(pushSettled, {})] { fn(); });
  }

  bool RemoteSyncController::holdCloseForPush(std::function<void()> reclose) {
    if (pushCloseHeld || (!pushTimer->isActive() && !*remotePushing)) return false;
    pushCloseHeld = true;
    pushSettled = std::move(reclose);
    if (!*remotePushing) { pushTimer->stop(); firePush(); }
    QTimer::singleShot(RESULT_CLOSE_CAP_MS, this, [this] {
      if (pushSettled) QTimer::singleShot(0, this, [fn = std::exchange(pushSettled, {})] { fn(); });
    });
    return true;
  }

  void RemoteSyncController::scheduleResultUpload() {
    if (!resultDirty) resultDirtySince = QDateTime::currentMSecsSinceEpoch();
    resultDirty = true;
    if (!resultInFlight) armResultTimer();
  }

  // due = max(last + gap, min(now + idle, dirtySince + gap)): idle-debounced, capped, spaced.
  void RemoteSyncController::armResultTimer() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 due = std::min(now + resultIdleMs, resultDirtySince + resultGapMs);
    if (lastResultAt > 0) due = std::max(due, lastResultAt + resultGapMs);
    if (resultSettled) due = now;   // a flush is waiting on it
    resultTimer->start(static_cast<int>(std::clamp<qint64>(due - now, 0, resultGapMs)));
  }

  void RemoteSyncController::startResultUpload() {
    resultDirty = false;
    resultInFlight = true;
    lastResultAt = QDateTime::currentMSecsSinceEpoch();
    const int seq = ++resultSeq;
    QPointer<RemoteSyncController> self(this);
    auto done = [this, self, seq] {
      if (!self || seq != resultSeq) return;
      resultInFlight = false;
      if (resultDirty) { armResultTimer(); return; }
      if (resultSettled) std::exchange(resultSettled, {})();
    };
    if (h.uploadResult) h.uploadResult(done);
    else done();
  }

  bool RemoteSyncController::flushResultUpload(std::function<void()> settled) {
    if (!resultBusy() || session->address().isEmpty()) return false;
    resultSettled = std::move(settled);
    if (resultDirty && !resultInFlight) armResultTimer();
    return true;
  }

  bool RemoteSyncController::holdCloseForResult(std::function<void()> reclose) {
    if (closeHeld || !flushResultUpload(std::move(reclose))) return false;
    closeHeld = true;
    QTimer::singleShot(RESULT_CLOSE_CAP_MS, this, [this] {
      if (resultSettled) std::exchange(resultSettled, {})();
    });
    return true;
  }

}  // namespace stencil::gui

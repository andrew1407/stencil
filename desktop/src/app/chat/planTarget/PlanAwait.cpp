#include "PlanAwait.hpp"

#include "PopoverHost.hpp"

#include <QMetaObject>
#include <QTimer>

namespace stencil::gui {

  PlanAwait::PlanAwait(QObject& owner, PopoverHost& host, llm::OpDone done, bool lapseOk,
                       const QString& lapseErr)
      : QObject(&owner), host(host), done(std::move(done)), lapseOk(lapseOk), lapseErr(lapseErr) {
    host.awaits.append(this);
  }

  void PlanAwait::start(QObject& owner, PopoverHost& host, llm::OpDone done,
                        const std::function<void(PlanAwait*)>& work, int timeoutMs, bool lapseOk,
                        const QString& lapseErr) {
    auto* await = new PlanAwait(owner, host, std::move(done), lapseOk, lapseErr);
    if (timeoutMs > 0) QTimer::singleShot(timeoutMs, await, [await] { await->lapse(); });
    work(await);
    await->starting = false;
    if (await->settled) await->respond();
  }

  void PlanAwait::settle(bool ok, const QString& err) {
    if (settled) return;
    settled = true;
    this->ok = ok;
    this->err = err;
    host.awaits.removeAll(this);
    if (starting) return;   // start() answers a synchronous outcome itself
    QMetaObject::invokeMethod(this, [this] { respond(); }, Qt::QueuedConnection);
  }

  void PlanAwait::respond() {
    const llm::OpDone answerTo = std::move(done);
    done = nullptr;
    deleteLater();
    if (answerTo) answerTo(ok, err);
  }

}  // namespace stencil::gui

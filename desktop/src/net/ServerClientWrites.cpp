#include "ServerClient.hpp"

#include <memory>

namespace stencil::net {

  void ServerClient::deleteProjectAsync(const QString& id, std::function<void(bool)> done) {
    requestAsync("DELETE", QString("/projects/%1").arg(id), {}, {},
                 [done = std::move(done)](int status, QByteArray) {
                   if (!isOkStatus(status)) {
                     done(false);
                     return;
                   }
                   done(true);
                 });
  }

  void ServerClient::runGuardedWriteAsync(
      int attempts, qint64 startVersion,
      std::function<void(qint64, std::function<void(GuardOutcome)>)> attempt,
      std::function<void(qint64, std::function<void(bool, qint64)>)> resolve,
      std::function<void(GuardOutcome)> done) {
    // Heap-managed loop state so the recursion survives across async hops.
    struct State {
      int i = 0;
      qint64 version = 0;
      int attempts = 0;
      std::function<void(qint64, std::function<void(GuardOutcome)>)> attempt;
      std::function<void(qint64, std::function<void(bool, qint64)>)> resolve;
      std::function<void(GuardOutcome)> done;
      std::function<void()> step;
    };
    auto st = std::make_shared<State>();
    st->version = startVersion;
    st->attempts = attempts;
    st->attempt = std::move(attempt);
    st->resolve = std::move(resolve);
    st->done = std::move(done);
    // Weak here: a strong self-capture would keep the state, and every capture the caller's
    // lambdas hold, alive for the process. The in-flight callbacks own it instead.
    st->step = [weak = std::weak_ptr<State>(st)]() {
      const std::shared_ptr<State> st = weak.lock();
      if (!st) return;
      st->attempt(st->version, [st](GuardOutcome o) {
        if (o != GuardOutcome::CONFLICT) {  // Committed or Failed → done
          st->done(o);
          return;
        }
        if (st->i + 1 >= st->attempts) {  // last attempt still conflicted → exhausted
          st->done(GuardOutcome::CONFLICT);
          return;
        }
        st->resolve(st->version, [st](bool ok, qint64 newVersion) {
          if (!ok) {  // resolve gave up (e.g. re-read failed)
            st->done(GuardOutcome::CONFLICT);
            return;
          }
          st->version = newVersion;
          ++st->i;
          st->step();
        });
      });
    };
    st->step();
  }
}  // namespace stencil::net


#pragma once
#include "planExecutor.hpp"

#include <QObject>
#include <QString>
#include <functional>

// One op of a plan or a script waiting on I/O, as a continuation: it settles once — the work's
// answer, its timeout, or a close (PopoverHost::awaits) — and answers inline while the work is
// still starting, else from the event loop, never inside the emitter that settled it.
namespace stencil::gui {

  class PopoverHost;

  class PlanAwait : public QObject {
   public:
    // `work` starts the I/O and settles the await from its callbacks. A timeout (`timeoutMs` > 0)
    // or a close settles it on (`lapseOk`, `lapseErr`): what the op reports when cut short.
    // `owner` (the window) outlives it; `host` lists it until it settles.
    static void start(QObject& owner, PopoverHost& host, llm::OpDone done,
                      const std::function<void(PlanAwait*)>& work, int timeoutMs = 0,
                      bool lapseOk = false, const QString& lapseErr = QString());

    void settle(bool ok, const QString& err);
    void lapse() { settle(lapseOk, lapseErr); }
    bool isSettled() const { return settled; }

   private:
    PlanAwait(QObject& owner, PopoverHost& host, llm::OpDone done, bool lapseOk,
              const QString& lapseErr);
    void respond();

    PopoverHost& host;
    llm::OpDone done;
    const bool lapseOk;
    const QString lapseErr;
    bool starting = true;
    bool settled = false;
    bool ok = false;
    QString err;
  };

}  // namespace stencil::gui

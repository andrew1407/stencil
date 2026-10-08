#include "MainWindow.hpp"
#include "ScriptHost.hpp"
#include "CanvasWidget.hpp"
#include "ScriptBuffer.hpp"
#include "fetchGuard.hpp"

#include <QDateTime>
#include <QTimer>

// What reaches the script from outside the window: a stencil:// link's script, opened in the
// Script window, never run (browser twin js/index.js openLaunchScript).
namespace stencil::gui {

  namespace {

    constexpr int PICTURE_POLL_MS = 100;

  }  // namespace

  void ScriptHost::adoptLinkedScript(const QString& text, std::optional<qint64> pictureBefore) {
    const auto deliver = [this, text] {
      model::ScriptBuffer::instance().setText(text);
      w.acts.script->trigger();
    };
    if (!pictureBefore) {
      QTimer::singleShot(0, &w, deliver);
      return;
    }
    // The linked picture decodes off the event loop: wait for the canvas to show another one,
    // bounded by the fetch deadline, so a failed load still hands the script over.
    auto* poll = new QTimer(&w);
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + net::fetchGuard::fetchTimeoutMs() * 2;
    const qint64 before = *pictureBefore;
    QObject::connect(poll, &QTimer::timeout, &w, [this, poll, before, deadline, deliver] {
      const bool landed = w.canvas->hasImage() && w.canvas->getOriginalImage().cacheKey() != before;
      if (!landed && QDateTime::currentMSecsSinceEpoch() < deadline) return;
      poll->stop();
      poll->deleteLater();
      deliver();
    });
    poll->start(PICTURE_POLL_MS);
  }

}  // namespace stencil::gui

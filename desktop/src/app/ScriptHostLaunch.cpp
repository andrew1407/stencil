#include "MainWindow.hpp"
#include "ScriptHost.hpp"
#include "CanvasWidget.hpp"
#include "ChatPlanTarget.hpp"
#include "Notifications.hpp"
#include "ScriptBuffer.hpp"
#include "ScriptDoc.hpp"
#include "fetchGuard.hpp"
#include "fileStore.hpp"
#include "modalChrome.hpp"
#include "scriptRun.hpp"

#include <QDateTime>
#include <QDir>
#include <QTimer>

#include <memory>

// What reaches the script from outside the window: a stencil:// link's script (browser twin
// js/index.js runLaunchScript).
namespace stencil::gui {

  namespace {

    constexpr int PICTURE_POLL_MS = 100;
    constexpr int PREVIEW_LINES = 8;   // how much of a linked script the confirm shows

    QString preview(const QString& text) {
      const QStringList lines = text.split(QLatin1Char('\n'));
      QString shown = lines.mid(0, PREVIEW_LINES).join(QLatin1Char('\n'));
      if (lines.size() > PREVIEW_LINES) shown += QStringLiteral("\n…");
      return shown;
    }

  }  // namespace

  void ScriptHost::adoptLinkedScript(const QString& text, bool openOnly, std::optional<qint64> pictureBefore) {
    const auto deliver = [this, text, openOnly] {
      model::ScriptBuffer::instance().setText(text);
      if (openOnly) w.acts.script->trigger();
      else runLinkedScript(text);
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

  // A link is clickable from any page, so its script never runs unasked, and never reads a local file.
  void ScriptHost::runLinkedScript(const QString& text) {
    bool run = false;
    if (confirmLinkedRun) {
      run = confirmLinkedRun(text);
    } else {
      ConfirmSpec spec;
      spec.title = MainWindow::tr("Run a linked script");
      spec.titleIcon = QStringLiteral("script");
      spec.message = MainWindow::tr("Run this script from a link?\n\n%1\n\nIt may open web images only.")
                         .arg(preview(text));
      spec.confirmLabel = MainWindow::tr("Run");
      spec.confirmIcon = QStringLiteral("play");
      spec.cancelLabel = MainWindow::tr("Just open it");
      run = confirmModal(&w, spec);
    }
    if (!run) {
      w.acts.script->trigger();   // declined: the Script window, with the text in it
      return;
    }
    const auto target = std::make_shared<ChatPlanTarget>(w);
    runScriptThen(model::ScriptDoc::parse(text), *target, [this, target](const ScriptRunResult& result) {
      reportRun(result);
      if (result.ops > 0) refreshAfterScript();
    }, ScriptRunRules{/*webSourcesOnly=*/true});
  }

}  // namespace stencil::gui

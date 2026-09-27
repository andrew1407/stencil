// The live editor's awaited ops (llm-contract §10 connect / openUrl / openFile, §2 frame, the .stc
// @frame reload): each starts its I/O and answers the run's continuation once it lands (PlanAwait).
#include "ChatPlanTarget.hpp"

#include "MainWindow.hpp"
#include "PlanAwait.hpp"
#include "../../../net/connectionStore.hpp"
#include "../../../net/ServerClient.hpp"
#include "../../../net/fetchGuard.hpp"
#include "../../../support/notify/Notifications.hpp"

#include <QPointer>

namespace stencil::gui {

  void ChatPlanTarget::connectServerThen(const QString& server, llm::OpDone done) {
    // Resolve ONLY against the user's SAVED servers; plans never carry tokens or hosts (contract §10).
    const auto saved = stencil::net::connectionStore::loadSavedServers();
    QStringList urls;
    for (const auto& s : saved) urls << s.url;
    const QString url = llm::resolveServerRef(server, urls);
    if (url.isEmpty())
      return done(false, QStringLiteral("connect: unknown server \"%1\" — only a server you have "
                                        "already saved can be used")
                             .arg(server));
    QString token;
    auto kind = stencil::net::ServerClient::CredentialKind::NONE;
    for (const auto& s : saved)
      if (s.url == url) { token = s.token; kind = stencil::net::ServerClient::kindFromTag(s.kind); break; }
    // Bounded by the fetch timeout, a lapse with no reason: a silent handshake cannot hang the plan.
    PlanAwait::start(
        w, w.pop,
        [this, url, done](bool ok, const QString& cerr) {
          if (!ok)
            return done(false, QStringLiteral("connect: %1")
                                   .arg(cerr.isEmpty() ? QStringLiteral("timed out") : cerr));
          w.notify->success(QStringLiteral("Connected to %1").arg(url));
          done(true, QString());
        },
        [&](PlanAwait* await) {
          w.ensureConnections()->connectToAsync(
              url, token,
              [guard = QPointer<PlanAwait>(await)](bool ok, QString err) {
                if (guard) guard->settle(ok, err);
              },
              kind);
        },
        net::fetchGuard::fetchTimeoutMs());
  }

  // §10 openUrl: the SAME async path as the dialog's "open here", answered once the picture lands.
  void ChatPlanTarget::openUrlThen(const QString& url, bool incognito, llm::OpDone done) {
    // Say what happened in OUR words — a silent download reads as "nothing happened".
    w.notify->info(QStringLiteral("Opening %1%2")
                         .arg(url, incognito ? QStringLiteral(" (incognito)") : QString()));
    w.parts.sourceOpener.chatLoadSourceThen(url, incognito, 0, [done](bool ok, const QString& why) {
      done(ok, ok ? QString() : QStringLiteral("openUrl: %1").arg(why));
    });
  }

  // §10 openFile: the same answer for a LOCAL path; a .stencil or .json takes its own path.
  void ChatPlanTarget::openFileThen(const QString& path, llm::OpDone done) {
    w.parts.sourceOpener.chatOpenFileThen(path, std::move(done));
  }

  void ChatPlanTarget::extractFramesThen(const QVector<int>& indices, llm::OpDone done) {
    w.parts.chatAppliers.chatExtractFramesThen(indices, std::move(done));
  }

  // §10 @frame: the block's own source reloaded AT that frame, so it becomes the working image.
  void ChatPlanTarget::openSourceFrameThen(const QString& spec, int frame, llm::OpDone done) {
    if (spec.isEmpty()) return done(false, QStringLiteral("frame: this block names no source"));
    w.parts.sourceOpener.chatLoadSourceThen(spec, w.incognito, frame,
                                            [done](bool ok, const QString& why) {
                                              done(ok, ok ? QString() : QStringLiteral("frame: %1").arg(why));
                                            });
  }

}  // namespace stencil::gui

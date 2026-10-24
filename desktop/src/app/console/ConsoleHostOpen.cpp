#include "ConsoleHost.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "ChatPlanTarget.hpp"
#include "ChatSessionController.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "SessionKey.hpp"
#include "displayName.hpp"
#include "../../support/localPath.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

// The session flows: pictures and projects in, the servers, the assistant and the browser
// hand-off. Browser twin: the facade calls in js/console/shell/handlersSession.js.

namespace stencil::gui {

  using support::ConsoleOutput;

  namespace {
    bool isWebUrl(const QString& ref) {
      return ref.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) || ref.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
    }
    QString when(qint64 ms) { return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString(); }
    QString orDefault(const QString& v, const char* fallback) { return v.isEmpty() ? QString::fromLatin1(fallback) : v; }
    // Every column but the last padded to its widest cell, two spaces between.
    QStringList padColumns(const QVector<QStringList>& rows) {
      QVector<int> widths;
      for (const QStringList& r : rows)
        for (int i = 0; i < r.size(); ++i) {
          if (widths.size() <= i) widths.push_back(0);
          widths[i] = qMax(widths[i], int(r[i].size()));
        }
      QStringList out;
      for (const QStringList& r : rows) {
        QString line;
        for (int i = 0; i < r.size(); ++i) line += (i + 1 == r.size() ? r[i] : r[i].leftJustified(widths[i])) + (i + 1 == r.size() ? QString() : QStringLiteral("  "));
        out << line;
      }
      return out.isEmpty() ? QStringList{QStringLiteral("(no projects)")} : out;
    }
    const stencil::net::ServerProject* matchServerProject(const QVector<stencil::net::ServerProject>& all, const QString& ref, const QString& url) {
      for (const auto& p : all)
        if ((p.id == ref || p.name.compare(ref, Qt::CaseInsensitive) == 0) && (url.isEmpty() || p.serverUrl == url)) return &p;
      return nullptr;
    }
  }  // namespace

  QString ConsoleHost::localProjectId(const QString& ref) const {
    for (const Project& p : w.projectList)
      if (QString::fromStdString(p.meta.id) == ref || QString::compare(QString::fromStdString(p.meta.name), ref, Qt::CaseInsensitive) == 0)
        return QString::fromStdString(p.meta.id);
    return {};
  }

  void ConsoleHost::upload(const QString& ref, support::ConsoleDone done) {
    if (ref.isEmpty()) {
      w.parts.sourceOpener.openImageDialog(false);
      return done(ConsoleOutput::plain(QStringLiteral("pick a file in the Open Image window")));
    }
    const auto landed = [done, ref](bool ok, const QString& err) {
      done(ok ? ConsoleOutput::plain(QStringLiteral("loaded ") + ref) : ConsoleOutput::error(err));
    };
    if (isWebUrl(ref)) return ChatPlanTarget(w).openUrlThen(ref, w.incognito, landed);
    if (!QFileInfo::exists(support::expandHomePath(ref)))
      return done(ConsoleOutput::error(QStringLiteral("no file at %1 — give a path or an http(s) URL").arg(ref)));
    ChatPlanTarget(w).openFileThen(ref, landed);
  }

  void ConsoleHost::open(const QString& ref, support::ConsoleDone done) {
    if (openLaunchLink(ref, done)) return;   // ConsoleHostLaunch.cpp
    if (isWebUrl(ref) || QFileInfo::exists(support::expandHomePath(ref))) return upload(ref, std::move(done));
    const QString id = localProjectId(ref);
    if (!id.isEmpty()) {
      const QString name = support::shortName(QString::fromStdString(w.findProject(id.toStdString())->meta.name));
      const bool known = w.loadProjectIntoCanvas(id, true, [done, name](bool ok) {
        done(ok ? ConsoleOutput::plain(QStringLiteral("opened ") + name) : ConsoleOutput::error(QStringLiteral("could not open ") + name));
      });
      if (!known) done(ConsoleOutput::error(QStringLiteral("could not open ") + name));
      return;
    }
    if (!w.remote.connections)
      return done(ConsoleOutput::error(QStringLiteral("no project called '%1' — '/projects' lists them").arg(ref)));
    w.remote.connections->sharedProjectsAsync([this, ref, done](QVector<stencil::net::ServerProject> all) {
      const auto* p = matchServerProject(all, ref, QString());
      if (!p) return done(ConsoleOutput::error(QStringLiteral("no project called '%1' — '/projects' lists them").arg(ref)));
      w.parts.projects.openServerProject(p->serverUrl, p->id);
      done(ConsoleOutput::plain(QStringLiteral("opening %1 from %2").arg(p->name, p->serverUrl)));
    });
  }

  QString ConsoleHost::openInNewWindow(const QString& ref) {
    const QString id = localProjectId(ref);
    if (id.isEmpty()) return QStringLiteral("no saved project called '%1' — 'ls' lists them").arg(ref);
    w.parts.projects.openProjectInNewWindow(id);
    return {};
  }

  // `*` marks the open project; the long form (`ll`) adds the size, colour and date columns.
  QStringList ConsoleHost::localProjectsRows(bool longForm) const { return padColumns(localProjectCells(longForm)); }

  QVector<QStringList> ConsoleHost::localProjectCells(bool longForm) const {
    QVector<QStringList> rows;
    for (const Project& p : w.projectList) {
      const QString id = QString::fromStdString(p.meta.id);
      QStringList row{(id == w.activeProjectId ? QStringLiteral("* ") : QStringLiteral("  ")) + orDefault(QString::fromStdString(p.meta.name), "Untitled"),
                      QStringLiteral("local")};
      if (longForm)
        row << (p.meta.imageW > 0 ? QStringLiteral("%1x%2").arg(p.meta.imageW).arg(p.meta.imageH) : QString())
            << QString::fromStdString(p.meta.color) << when(p.meta.updatedAt);
      rows.push_back(row);
    }
    return rows;
  }

  void ConsoleHost::projectsThen(const QString& serverUrl, bool longForm, std::function<void(QStringList)> done) {
    QVector<QStringList> rows = serverUrl.isEmpty() ? localProjectCells(longForm) : QVector<QStringList>{};
    if (!w.remote.connections) return done(padColumns(rows));
    w.remote.connections->sharedProjectsAsync([serverUrl, longForm, rows, done](QVector<stencil::net::ServerProject> all) mutable {
      for (const auto& p : all) {
        if (!serverUrl.isEmpty() && p.serverUrl != serverUrl) continue;
        QStringList row{QStringLiteral("  ") + orDefault(p.name, "Untitled"), QStringLiteral("server ") + p.serverUrl};
        if (longForm) row << (p.imageW > 0 ? QStringLiteral("%1x%2").arg(p.imageW).arg(p.imageH) : QString()) << p.color << when(p.updatedAt);
        rows.push_back(row);
      }
      done(padColumns(rows));
    });
  }

  QStringList ConsoleHost::connectionsRows() const {
    QStringList out;
    if (w.remote.connections)
      for (const stencil::net::ServerClient* c : w.remote.connections->getClients()) out << QStringLiteral("  ") + c->getBase();
    return out.isEmpty() ? QStringList{QStringLiteral("(no server connections)")} : out;
  }

  void ConsoleHost::connectThen(const QString& url, const QString& token, std::function<void(QString err)> done) {
    using Kind = stencil::net::ServerClient::CredentialKind;
    w.ensureConnections()->connectToAsync(url, token, [this, done](bool ok, QString err) {
      if (ok) w.warnInsecureConnections();
      done(ok ? QString() : err);
    }, token.isEmpty() ? Kind::NONE : Kind::SESSION);
  }

  QString ConsoleHost::disconnect(const QString& url) {
    if (!w.remote.connections || w.remote.connections->getClients().isEmpty()) return QStringLiteral("no server connections");
    if (!url.isEmpty() && !w.remote.connections->find(url)) return QStringLiteral("not connected to %1").arg(url);
    w.remote.connections->disconnectFrom(url);
    return {};
  }

  void ConsoleHost::fetchThen(const QString& name, const QString& serverUrl, support::ConsoleDone done) {
    if (!w.remote.connections) return done(ConsoleOutput::error(QStringLiteral("no server connections — '/connect <url>' first")));
    w.remote.connections->sharedProjectsAsync([this, name, serverUrl, done](QVector<stencil::net::ServerProject> all) {
      const auto* p = matchServerProject(all, name, serverUrl);
      if (!p) return done(ConsoleOutput::error(QStringLiteral("no server project called '%1'").arg(name)));
      w.parts.projects.openServerProject(p->serverUrl, p->id);
      done(ConsoleOutput::plain(QStringLiteral("fetched %1 from %2").arg(p->name, p->serverUrl)));
    });
  }

  void ConsoleHost::browser(bool incognito, support::ConsoleDone done) {
    QString err;
    const QString url = w.parts.projects.browserLaunchUrl(incognito, &err);
    if (url.isEmpty()) return done(ConsoleOutput::error(err));
    QDesktopServices::openUrl(QUrl(url));
    done(ConsoleOutput::plain(QStringLiteral("opened in the browser")));
  }

  QStringList ConsoleHost::llm(const QString& key, const QString& value, QString* err) {
    const QString k = key.toLower();
    const auto label = [this](const QString& name) {
      if (name == QLatin1String("provider")) return QStringLiteral("provider: ") + orDefault(w.settings.llmProvider, "(unset)");
      if (name == QLatin1String("url")) return QStringLiteral("url: ") + orDefault(w.settings.llmBaseUrl, "(unset)");
      if (name == QLatin1String("model")) return QStringLiteral("model: ") + orDefault(w.settings.llmModel, "(unset)");
      const bool anthropic = w.settings.llmProvider == QLatin1String("anthropic");
      const bool set = anthropic ? !llm::SessionKey::instance().key().isEmpty() : !w.settings.llmApiKey.isEmpty();
      return QStringLiteral("key: ") + QString::fromLatin1(set ? (anthropic ? "(set, hidden)" : "(set)") : "(unset)");
    };
    if (k.isEmpty()) return {label(QStringLiteral("provider")), label(QStringLiteral("url")), label(QStringLiteral("model"))};
    static const QStringList KEYS{QStringLiteral("provider"), QStringLiteral("url"), QStringLiteral("model"), QStringLiteral("key")};
    if (!KEYS.contains(k)) {
      *err = QStringLiteral("llm takes 'provider | url | model | key <value>'");
      return {};
    }
    if (value.isEmpty()) return {label(k)};
    // An Anthropic key is a session key (llm-providers.md section 5): in memory, never in the settings file.
    if (k == QLatin1String("key") && w.settings.llmProvider == QLatin1String("anthropic")) {
      if (value.compare(QLatin1String("forget"), Qt::CaseInsensitive) == 0) llm::SessionKey::instance().forget();
      else llm::SessionKey::instance().hold(value);
      return {label(k)};
    }
    Settings s = w.settings;
    (k == QLatin1String("provider") ? s.llmProvider : k == QLatin1String("url") ? s.llmBaseUrl
     : k == QLatin1String("model") ? s.llmModel : s.llmApiKey) =
        k == QLatin1String("key") && value.compare(QLatin1String("forget"), Qt::CaseInsensitive) == 0 ? QString() : value;
    w.applySettings(s, true);
    return {k + QStringLiteral(" set")};
  }

  QString ConsoleHost::prompt(const QString& text) {
    if (!w.chatSession) return QStringLiteral("there is no assistant here");
    w.chatSession->onChatSend(text);
    return {};
  }

  bool ConsoleHost::syncToServer() const { return w.settings.syncToServer; }
  void ConsoleHost::setSyncToServer(bool on) {
    Settings s = w.settings;
    s.syncToServer = on;
    w.applySettings(s, true);
  }

}  // namespace stencil::gui

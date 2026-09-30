#pragma once
// An in-process stand-in for the Stencil server's REST routes (server/internal/protocol is the
// contract): projects with a version guard on PUT, the file kinds that bump it, and the session
// probe. One reply per connection, as the client's own requests are.
#include <QByteArray>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <utility>
#include <memory>

namespace stencil::test {

  struct MockProject {
    QString name, color;
    QStringList keywords;
    qint64 expiresAt = 0;
    qint64 version = 1;
    QJsonObject layout;
    QByteArray original;
  };

  struct MockRest {
    QTcpServer server;
    QMap<QString, MockProject> projects;
    int originalGets = 0, puts = 0, putAttempts = 0, resultPosts = 0, created = 0;
    int putStatus = 0;   // non-zero: every PUT answers this instead
    bool withHash = true;   // false: records carry no originalHash, as an older server's
    int pageSize = 0;       // non-zero: GET /projects answers pages this long, a full one with a nextCursor
    QString stuckCursor;    // non-empty: every list page hands back this cursor, as a looping server
    bool endlessCursors = false;   // every list page hands back a fresh cursor
    int listGets = 0;
    QString lastAfter;      // the decoded ?after= of the last list request
    QJsonObject lastCreate;   // the body of the last POST /projects

    bool listen() {
      QObject::connect(&server, &QTcpServer::newConnection, [this] {
        while (QTcpSocket* s = server.nextPendingConnection()) {
          auto buf = std::make_shared<QByteArray>();
          QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
            *buf += s->readAll();
            const int headEnd = buf->indexOf("\r\n\r\n");
            if (headEnd < 0) return;
            static const QRegularExpression lenRe(QStringLiteral("(?i)content-length:\\s*(\\d+)"));
            const auto m = lenRe.match(QString::fromLatin1(buf->left(headEnd)));
            const int len = m.hasMatch() ? m.captured(1).toInt() : 0;
            if (buf->size() < headEnd + 4 + len) return;
            const QByteArray line = buf->left(buf->indexOf("\r\n"));
            reply(s, route(QString::fromLatin1(line), buf->mid(headEnd + 4, len)));
          });
          QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
      });
      return server.listen(QHostAddress::LocalHost, 0);
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

    struct Out {
      int status = 200;
      QByteArray body = "{}";
      QByteArray type = "application/json";
    };

    static QByteArray json(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
    QJsonObject record(const QString& id, const MockProject& p) const {
      QJsonObject o{{"id", id}, {"name", p.name}, {"color", p.color}, {"version", double(p.version)},
                    {"hasImage", !p.original.isEmpty()}};
      if (!p.keywords.isEmpty()) o.insert("keywords", QJsonArray::fromStringList(p.keywords));
      if (p.expiresAt) o.insert("expiresAt", double(p.expiresAt));
      if (withHash && !p.original.isEmpty())
        o.insert("originalHash", QString::fromLatin1(
                                     QCryptographicHash::hash(p.original, QCryptographicHash::Sha256).toHex()));
      return o;
    }

    // Keyset pages over the ids, as server/internal/httpapi/projectlist.go pages over (updatedAt, id).
    Out listPage(const QString& query) {
      ++listGets;
      // A raw '+' is a space to Go's url.ParseQuery, so a cursor must travel percent-encoded.
      const QString form = query.mid(1).replace(QLatin1Char('+'), QStringLiteral("%20"));
      lastAfter = QUrlQuery(form).queryItemValue(QStringLiteral("after"), QUrl::FullyDecoded);
      QJsonArray list;
      for (auto it = std::as_const(projects).upperBound(lastAfter);
           it != projects.cend() && (!pageSize || list.size() < pageSize); ++it)
        list.append(record(it.key(), *it));
      QJsonObject out{{"projects", list}};
      if (!stuckCursor.isEmpty()) out.insert("nextCursor", stuckCursor);
      else if (endlessCursors) out.insert("nextCursor", QStringLiteral("c%1").arg(listGets));
      else if (pageSize && list.size() == pageSize)
        out.insert("nextCursor", list.last().toObject().value("id").toString());
      return {200, json(out)};
    }

    Out route(const QString& line, const QByteArray& body) {
      static const QRegularExpression re(
          QStringLiteral("^(\\w+) /([^ ?]*)(\\?\\S*)? HTTP"));
      const auto m = re.match(line);
      const QString verb = m.captured(1);
      const QStringList path = m.captured(2).split(QLatin1Char('/'), Qt::SkipEmptyParts);
      if (path.value(0) == QLatin1String("auth"))
        return path.value(1) == QLatin1String("token") ? Out{200, "{\"token\":\"tok\"}"}
                                                       : Out{200, "{\"sessionId\":\"s\",\"expiresAt\":0}"};
      if (path.value(0) != QLatin1String("projects")) return {404};
      if (path.size() == 1 && verb == QLatin1String("GET")) return listPage(m.captured(3));
      if (path.size() == 1 && verb == QLatin1String("POST")) {
        const QString id = QStringLiteral("p%1").arg(++created);
        lastCreate = QJsonDocument::fromJson(body).object();
        projects[id].name = lastCreate.value("name").toString();
        return {200, json(record(id, projects[id]))};
      }
      if (!projects.contains(path.value(1))) return {404};
      MockProject& p = projects[path.value(1)];
      if (path.size() == 4 && verb == QLatin1String("GET") && path.value(3) == QLatin1String("original")) {
        ++originalGets;
        return {200, p.original, "image/png"};
      }
      if (path.size() == 4 && verb == QLatin1String("POST")) {
        if (path.value(3) == QLatin1String("original")) p.original = body;
        if (path.value(3) == QLatin1String("result")) ++resultPosts;
        ++p.version;
        return {200};
      }
      if (path.size() == 2 && verb == QLatin1String("GET"))
        return {200, json({{"project", record(path.value(1), p)}, {"layout", p.layout}})};
      if (path.size() == 2 && verb == QLatin1String("PUT")) {
        ++putAttempts;
        if (putStatus) return {putStatus};
        const QJsonObject in = QJsonDocument::fromJson(body).object();
        if (qint64(in.value("version").toDouble()) != p.version) return {409};
        ++puts;
        if (in.contains("name")) p.name = in.value("name").toString();
        if (in.contains("color")) p.color = in.value("color").toString();
        if (in.contains("layout")) p.layout = in.value("layout").toObject();
        return {200, json({{"version", double(++p.version)}})};
      }
      if (path.size() == 2 && verb == QLatin1String("DELETE")) {
        projects.remove(path.value(1));
        return {204, {}};
      }
      return {404};
    }

    static void reply(QTcpSocket* s, const Out& out) {
      s->write("HTTP/1.1 " + QByteArray::number(out.status) + " X\r\nContent-Type: " + out.type +
               "\r\nContent-Length: " + QByteArray::number(out.body.size()) +
               "\r\nConnection: close\r\n\r\n" + out.body);
      s->flush();
      s->disconnectFromHost();
    }
  };

}  // namespace stencil::test

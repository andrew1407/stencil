#include "launchUri.hpp"
#include "launchOptions.hpp"
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace stencil::gui::launchUri {

  namespace {
    // common/config/launchQuery.json through the qrc: the browser's vocabulary, read once.
    struct Grammar {
      QHash<QString, QString> canonical;   // spelling → canonical key
      QStringList secret, docks, trueWords, falseWords;
      QHash<QString, QString> themes;
    };
    const Grammar& grammar() {
      static const Grammar g = [] {
        Grammar out;
        QFile f(QStringLiteral(":/config/launchQuery.json"));
        if (!f.open(QIODevice::ReadOnly)) return out;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonObject keys = root.value("keys").toObject();
        for (auto it = keys.begin(); it != keys.end(); ++it)
          for (const QJsonValue& w : it.value().toArray()) out.canonical.insert(w.toString(), it.key());
        const auto list = [&](const char* key) {
          QStringList l;
          for (const QJsonValue& v : root.value(QLatin1String(key)).toArray()) l << v.toString();
          return l;
        };
        out.secret = list("secretKeys");
        out.docks = list("chatDocks");
        out.trueWords = list("trueWords");
        out.falseWords = list("falseWords");
        const QJsonObject themes = root.value("themeWords").toObject();
        for (auto it = themes.begin(); it != themes.end(); ++it) out.themes.insert(it.key(), it.value().toString());
        return out;
      }();
      return g;
    }
    const QStringList UNSAFE_KEYS = {"__proto__", "constructor", "prototype"};

    struct Pair { QString key, value; };

    // application/x-www-form-urlencoded: `+` is a space, then percent escapes decode.
    QString formDecode(const QString& raw) {
      QByteArray bytes = raw.toUtf8();
      bytes.replace('+', ' ');
      return QUrl::fromPercentEncoding(bytes);
    }
    // `mainColor`, `main_color` and `main-color` are one key.
    QString kebab(const QString& k) {
      static const QRegularExpression camelHump("([a-z0-9])([A-Z])");
      static const QRegularExpression edgeDashes("^-+|-+$");
      QString t = k.trimmed().replace(QLatin1Char('_'), QLatin1Char('-'));
      t.replace(camelHump, "\\1-\\2");
      return t.toLower().remove(edgeDashes);
    }
    QString camel(const QString& k) {
      QString out;
      for (int i = 0; i < k.size(); ++i) {
        const QChar next = i + 1 < k.size() ? k[i + 1] : QChar();
        if (k[i] == '-' && (next.isLower() || next.isDigit())) { out += next.toUpper(); ++i; }
        else out += k[i];
      }
      return out;
    }
    void pairsOf(const QString& raw, QList<Pair>& out) {
      for (const QString& part : raw.split('&', Qt::SkipEmptyParts)) {
        const int at = part.indexOf('=');
        const QString k = at < 0 ? part : part.left(at);
        const QString v = at < 0 ? QString() : part.mid(at + 1);
        out.append({kebab(formDecode(k)), formDecode(v)});
      }
    }
    std::optional<QString> text(const std::optional<QString>& v) {
      if (!v) return std::nullopt;
      const QString t = v->trimmed();
      return t.isEmpty() ? std::nullopt : std::optional<QString>(t);
    }
    // JS `Number(v)`: a finite number, else `fallback`.
    std::optional<double> numberOr(const std::optional<QString>& v, std::optional<double> fallback) {
      const auto t = text(v);
      if (!t) return fallback;
      bool ok = false;
      const double n = t->toDouble(&ok);
      return ok && std::isfinite(n) ? std::optional<double>(n) : fallback;
    }
    std::optional<double> sizeOr(const std::optional<QString>& v) {
      const auto n = numberOr(v, std::nullopt);
      return n && *n > 0 ? n : std::nullopt;
    }
    bool isHttp(const QString& s) { return s.startsWith("http://", Qt::CaseInsensitive) || s.startsWith("https://", Qt::CaseInsensitive); }
    // A bare flag, a 1 or a true word is on.
    bool flag(const QString& v) {
      const QString s = v.trimmed().toLower();
      return s.isEmpty() || s == "1" || grammar().trueWords.contains(s);
    }
    // Booleans by word, numbers by shape, the rest as typed.
    QJsonValue coerce(const QString& v) {
      const QString s = v.trimmed(), low = s.toLower();
      if (grammar().trueWords.contains(low)) return true;
      if (grammar().falseWords.contains(low)) return false;
      static const QRegularExpression number("^-?\\d+(\\.\\d+)?$");
      if (number.match(s).hasMatch()) return s.toDouble();
      return v;
    }
    std::optional<QJsonObject> jsonObject(const QString& s) {
      const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
      return doc.isObject() ? std::optional<QJsonObject>(doc.object()) : std::nullopt;
    }
  }  // namespace

  bool isLaunchQueryHash(const QString& hash) {
    return hash.startsWith('#') && hash.contains('=') && !hash.startsWith("#stencil=") &&
           !hash.startsWith("#stencil-desktop=");
  }

  std::optional<LaunchUri> parseParts(const QString& search, const QString& hash) {
    QList<Pair> pairs;
    pairsOf(search.startsWith('?') ? search.mid(1) : search, pairs);
    const bool fromHash = isLaunchQueryHash(hash);
    const int queryLen = pairs.size();
    if (fromHash) pairsOf(hash.mid(1), pairs);
    bool any = false;
    for (const Pair& p : pairs) any = any || p.key != "open";
    if (!any) return std::nullopt;

    LaunchUri q;
    q.fromHash = fromHash;
    struct Kept { QString canonical, value; };
    QList<Kept> kept;
    const Grammar& g = grammar();
    for (int i = 0; i < pairs.size(); ++i) {
      const Pair& p = pairs[i];
      if (!g.canonical.contains(p.key)) {
        const QString key = camel(p.key);
        if (!key.isEmpty() && !UNSAFE_KEYS.contains(key) && !q.settings.contains(key)) q.settings.insert(key, coerce(p.value));
        continue;
      }
      const QString canonical = g.canonical.value(p.key);
      if (g.secret.contains(canonical) && i < queryLen) { q.warnings.push_back({WarningCode::SECRET_IN_QUERY, canonical}); continue; }
      kept.append({canonical, p.value});
    }
    const auto first = [&](const char* key) -> std::optional<QString> {
      for (const Kept& k : kept) if (k.canonical == QLatin1String(key)) return k.value;
      return std::nullopt;
    };
    const auto warn = [&](WarningCode code, const char* key) { q.warnings.push_back({code, QLatin1String(key)}); };

    const auto server = text(first("server")), id = text(first("id"));
    if (server && id) {
      q.picture = ServerPicture{*server, *id, *numberOr(first("version"), 0.0)};
    } else if (const auto locator = text(first("project"))) {
      // `host[:port]/projects/<ref>` names the server too; `server=` supplies a missing one.
      const int at = locator->indexOf("/projects/");
      static const QRegularExpression slashes("/+$");
      const QString origin = (at < 0 ? QString() : locator->left(at)).remove(slashes);
      const QString ref = QString(at < 0 ? *locator : locator->mid(at + int(qstrlen("/projects/")))).remove(slashes);
      if (ref.isEmpty()) warn(WarningCode::BAD_VALUE, "project");
      else q.picture = ProjectPicture{origin.isEmpty() ? server : std::optional<QString>(origin), ref};
    } else if (const auto src = text(first("source"))) {
      if (isHttp(*src) || src->startsWith("data:", Qt::CaseInsensitive)) {
        q.picture = SourcePicture{*src, numberOr(first("frame"), std::nullopt)};
      } else warn(WarningCode::BAD_VALUE, "source");
    } else if (first("blank")) {
      q.picture = BlankPicture{text(first("blank")).value_or("#ffffff"), sizeOr(first("width")), sizeOr(first("height"))};
    }
    q.name = text(first("name"));
    if (const auto raw = text(first("layout-raw"))) {
      if (const auto data = jsonObject(*raw)) q.layout = Layout{QString(), data}; else warn(WarningCode::BAD_JSON, "layout-raw");
    } else if (const auto layout = text(first("layout"))) {
      if (layout->startsWith('{')) {
        if (const auto data = jsonObject(*layout)) q.layout = Layout{QString(), data}; else warn(WarningCode::BAD_JSON, "layout");
      } else if (isHttp(*layout)) q.layout = Layout{*layout, std::nullopt};
      else warn(WarningCode::BAD_VALUE, "layout");
    }
    if (const auto v = first("incognito")) q.incognito = flag(*v);
    const QString script = first("script").value_or(QString());
    if (script.size() > launchScriptMaxChars()) warn(WarningCode::BAD_VALUE, "script"); else q.script = script;
    if (const auto t = first("theme")) {
      const QString word = t->trimmed().toLower();
      if (g.themes.contains(word)) q.theme = g.themes.value(word); else warn(WarningCode::BAD_VALUE, "theme");
    }
    if (const auto mc = text(first("main-color"))) q.settings.insert("mainTheme", *mc);
    q.description = text(first("description"));
    q.keywords = text(first("keywords"));
    q.color = text(first("project-color"));
    q.expire = text(first("expire"));
    q.llm.provider = text(first("llm-provider"));
    q.llm.model = text(first("llm-model"));
    q.llm.baseUrl = text(first("llm-base-url"));
    q.llm.serverUrl = text(first("llm-server-url"));
    q.llm.apiKey = text(first("llm-api-key"));
    if (const auto c = first("chat")) q.chatOpen = flag(*c) || c->trimmed().toLower() == "open";
    if (const auto d = first("chat-dock")) {
      const QString dock = d->trimmed().toLower();
      if (g.docks.contains(dock)) q.chatDock = dock; else warn(WarningCode::BAD_VALUE, "chat-dock");
    }
    QStringList tokens;
    for (const Kept& k : kept) if (k.canonical == "token") tokens << k.value;
    int n = 0;
    for (const Kept& k : kept) {
      if (k.canonical != "connect") continue;
      const QString token = n < tokens.size() ? tokens[n] : QString();
      ++n;
      if (const auto url = text(k.value)) q.connect.push_back({*url, token});
    }
    q.opensSession = q.picture.has_value() || q.incognito || !q.script.isEmpty();
    return q;
  }

  std::optional<LaunchUri> parse(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    if (scheme != "http" && scheme != "https" && scheme != "stencil") return std::nullopt;
    const QString search = url.hasQuery() ? QStringLiteral("?") + url.query(QUrl::FullyEncoded) : QString();
    const QString hash = url.hasFragment() ? QStringLiteral("#") + url.fragment(QUrl::FullyEncoded) : QString();
    return parseParts(search, hash);
  }

}  // namespace stencil::gui::launchUri

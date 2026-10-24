#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>
#include <optional>
#include <variant>
#include <vector>

// The launch URI (contracts/launch/launch-uri.md): the browser app's `?key=value` query, the same
// pairs in its `#fragment`, or a `stencil://open?…` link, read into one struct the way
// browser/js/core/launch/query.js reads it; corpus common/fixtures/deepLink/launchQuery.json.
// The desktop's subset is adopted by launchOptions.cpp.
namespace stencil::gui::launchUri {

  struct ServerPicture { QString url, id; double version = 0; };
  // `host[:port]/projects/<ref>` or a bare `<ref>` (id or name); no server = the launch's, else local.
  struct ProjectPicture { std::optional<QString> server; QString ref; };
  struct SourcePicture { QString url; std::optional<double> frame; };   // frame: seconds into a video
  struct BlankPicture { QString color; std::optional<double> width, height; };   // px
  // server+id beats project beats source beats blank.
  using Picture = std::variant<ServerPicture, ProjectPicture, SourcePicture, BlankPicture>;

  // Fetched from `url`, or inline JSON (`layout-raw`, or a `layout` starting with `{`).
  struct Layout {
    QString url;
    std::optional<QJsonObject> data;
  };
  struct Connect { QString url, token; };
  // A credential in the query, unparsable JSON, or an unusable value.
  enum class WarningCode { SECRET_IN_QUERY, BAD_JSON, BAD_VALUE };
  struct Warning { WarningCode code; QString key; };
  struct Llm {
    std::optional<QString> provider, model, baseUrl, serverUrl, apiKey;
    bool any() const { return provider || model || baseUrl || serverUrl || apiKey; }
  };

  struct LaunchUri {
    std::optional<Picture> picture;
    std::optional<QString> name;
    std::optional<Layout> layout;
    bool incognito = false;
    QString script;
    std::optional<QString> theme;    // dark | light | system
    QJsonObject settings;            // every key outside the grammar, camel-cased and coerced
    Llm llm;
    bool chatOpen = false;
    std::optional<QString> chatDock;
    std::vector<Connect> connect;
    std::optional<QString> description, keywords, color, expire;
    std::vector<Warning> warnings;
    bool fromHash = false;
    bool opensSession = false;

    // The corpus shape, absent fields as null; a default LaunchUri is the empty query.
    QJsonObject toJson() const;
    // A picture, connection, layout, look, assistant or chat key: a picture URL with
    // grammar-looking parameters is not a link.
    bool actionable() const;
  };

  // `#stencil=` and `#stencil-desktop=` are the JSON hand-offs; any other `#a=b` is this grammar.
  bool isLaunchQueryHash(const QString& hash);
  // `search` with or without its `?`, `hash` with its `#`; nullopt when neither carries the grammar.
  std::optional<LaunchUri> parseParts(const QString& search, const QString& hash);
  // A whole http(s) or stencil: URL; nullopt for any other, or one carrying nothing of the grammar.
  std::optional<LaunchUri> parse(const QUrl& url);

}  // namespace stencil::gui::launchUri

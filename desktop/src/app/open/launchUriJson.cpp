#include "launchUri.hpp"
#include <QJsonArray>

// The launch URI as the corpus writes it (common/fixtures/deepLink/launchQuery.json), and what a
// surface counts as a link rather than a picture URL that happens to carry grammar-looking parameters.
namespace stencil::gui::launchUri {

  namespace {
    QJsonValue opt(const std::optional<QString>& v) { return v ? QJsonValue(*v) : QJsonValue(); }
    QJsonValue opt(const std::optional<double>& v) { return v ? QJsonValue(*v) : QJsonValue(); }

    // Each picture kind in the corpus shape.
    struct PictureJson {
      QJsonObject operator()(const ServerPicture& p) const {
        return {{"kind", "server"}, {"url", p.url}, {"id", p.id}, {"version", p.version}};
      }
      QJsonObject operator()(const ProjectPicture& p) const {
        return {{"kind", "project"}, {"server", opt(p.server)}, {"ref", p.ref}};
      }
      QJsonObject operator()(const SourcePicture& p) const {
        return {{"kind", "source"}, {"url", p.url}, {"frame", opt(p.frame)}};
      }
      QJsonObject operator()(const BlankPicture& p) const {
        return {{"kind", "blank"}, {"color", p.color}, {"width", opt(p.width)}, {"height", opt(p.height)}};
      }
    };

    QJsonValue layoutJson(const std::optional<Layout>& layout) {
      if (!layout) return QJsonValue();
      return layout->data ? QJsonObject{{"data", *layout->data}} : QJsonObject{{"url", layout->url}};
    }

    const char* warningWord(WarningCode code) {
      switch (code) {
        case WarningCode::SECRET_IN_QUERY: return "secret-in-query";
        case WarningCode::BAD_JSON: return "bad-json";
        case WarningCode::BAD_VALUE: return "bad-value";
      }
      return "";
    }
  }  // namespace

  bool LaunchUri::actionable() const {
    return opensSession || !connect.empty() || layout || theme || llm.any() || chatOpen || chatDock;
  }

  QJsonObject LaunchUri::toJson() const {
    QJsonObject o;
    o.insert("picture", picture ? QJsonValue(std::visit(PictureJson{}, *picture)) : QJsonValue());
    o.insert("name", opt(name));
    o.insert("layout", layoutJson(layout));
    o.insert("incognito", incognito);
    o.insert("script", script);
    o.insert("theme", opt(theme));
    o.insert("settings", settings);
    QJsonObject l;
    if (llm.provider) l.insert("provider", *llm.provider);
    if (llm.model) l.insert("model", *llm.model);
    if (llm.baseUrl) l.insert("baseUrl", *llm.baseUrl);
    if (llm.serverUrl) l.insert("serverUrl", *llm.serverUrl);
    if (llm.apiKey) l.insert("apiKey", *llm.apiKey);
    o.insert("llm", l);
    o.insert("chat", QJsonObject{{"open", chatOpen}, {"dock", opt(chatDock)}});
    QJsonArray c;
    for (const Connect& x : connect) c.append(QJsonObject{{"url", x.url}, {"token", x.token}});
    o.insert("connect", c);
    o.insert("project", QJsonObject{{"description", opt(description)}, {"keywords", opt(keywords)},
                                    {"color", opt(color)}, {"expire", opt(expire)}});
    QJsonArray w;
    for (const Warning& x : warnings) w.append(QJsonObject{{"code", warningWord(x.code)}, {"key", x.key}});
    o.insert("warnings", w);
    o.insert("fromHash", fromHash);
    o.insert("opensSession", opensSession);
    return o;
  }

}  // namespace stencil::gui::launchUri

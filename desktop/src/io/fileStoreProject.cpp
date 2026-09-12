#include "fileStore.hpp"
#include "fileStoreIo.hpp"
#include "deferredWrite.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>

namespace stencil::gui {

  bool fileStore::parseProjectFile(const QByteArray& bytes, ProjectFileData& out, QString* err) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (doc.isNull() || !doc.isObject()) {
      if (err) *err = QStringLiteral("Not valid JSON: ") + pe.errorString();
      return false;
    }
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != "stencil-project") {
      if (err) *err = QStringLiteral("Not a Stencil project file.");
      return false;
    }
    const int ver = o.value("version").toInt(0);
    if (ver < 1) {
      if (err) *err = QStringLiteral("Unrecognized project-file version.");
      return false;
    }
    if (ver > STENCIL_FILE_VERSION) {
      if (err) *err = QStringLiteral("This project needs a newer Stencil.");
      return false;
    }
    const QJsonObject img = o.value("image").toObject();
    const QString dataUrl = img.value("dataUrl").toString();
    const int b64 = dataUrl.indexOf("base64,");
    if (b64 < 0) {
      if (err) *err = QStringLiteral("Project file has no embedded image.");
      return false;
    }
    out.imageBytes = QByteArray::fromBase64(dataUrl.mid(b64 + 7).toLatin1());
    if (out.imageBytes.isEmpty()) {
      if (err) *err = QStringLiteral("Project image could not be decoded.");
      return false;
    }
    out.imageExt = img.value("ext").toString("png");
    out.imageWidth = img.value("w").toInt(0);
    out.imageHeight = img.value("h").toInt(0);
    out.layout = o.value("layout").toObject();
    out.name = o.value("name").toString("Untitled");
    out.color = o.value("color").toString();
    out.description = o.value("description").toString();
    out.keywords.clear();
    const QJsonArray kws = o.value("keywords").toArray();
    for (const auto& v : kws) out.keywords << v.toString();
    out.source = o.value("source").toString();
    out.resource = o.value("resource").toString();
    out.blank = o.value("blank").toBool(false);
    out.blankColor = o.value("blankColor").toString();
    const QJsonObject theme = o.value("theme").toObject();
    if (!theme.isEmpty()) {
      out.hasTheme = true;
      out.themeMode = theme.value("mode").toString();
      out.themeAccent = theme.value("accent").toString();
    }
    out.chat = o.value("chat").toObject();
    return true;
  }

  namespace {
    // §12.1 machinery filter (browser chatStore.js isInternalChatText parity):
    // the §7 continuation note (exact or bracketed variant, any role) and
    // assistant turns that are raw op-/ask-plans never enter the transcript.
    bool isInternalChatText(const QString& role, const QString& text) {
      const QString t = text.trimmed();
      if (t.isEmpty()) return false;
      static const QRegularExpression note(
          QStringLiteral(R"(^\[The working image is now\b[\s\S]*\]$)"));
      if (note.match(t).hasMatch()) return true;
      if (role != QLatin1String("assistant")) return false;
      static const QRegularExpression versionKey(QStringLiteral("\"version\"\\s*:"));
      static const QRegularExpression planKey(
          QStringLiteral("\"(actions|reply|variants|ask)\"\\s*:"));
      return (t.startsWith(QLatin1Char('{')) || t.startsWith(QLatin1Char('['))) &&
             t.contains(versionKey) && t.contains(planKey);
    }

    // Shared §12.1 whitelist for chat documents: rebuild each message so only a
    // valid role + string text survives (no images, no machinery text), then
    // bound the count. Saving (buildChatDoc) additionally drops empty texts.
    QJsonArray sanitizeChatMessages(const QJsonArray& messages, bool dropEmpty) {
      QJsonArray clean;
      for (const auto& v : messages) {
        const QJsonObject m = v.toObject();
        const QString role = m.value("role").toString();
        const QJsonValue textVal = m.value("text");
        if ((role != "user" && role != "assistant") || !textVal.isString()) continue;
        const QString text = textVal.toString();
        if (dropEmpty && text.isEmpty()) continue;
        if (isInternalChatText(role, text)) continue;
        QJsonObject out;
        out["role"] = role;
        out["text"] = text;
        clean.append(out);
      }
      while (clean.size() > fileStore::CHAT_DOC_MESSAGE_LIMIT) clean.removeFirst();
      return clean;
    }
  }  // namespace

  QJsonObject fileStore::buildChatDoc(const QJsonArray& messages, qint64 savedAt) {
    QJsonObject doc;
    doc["version"] = CHAT_DOC_VERSION;
    doc["savedAt"] = savedAt;
    doc["messages"] = sanitizeChatMessages(messages, /*dropEmpty=*/true);
    return doc;
  }

  QJsonArray fileStore::parseChatDoc(const QJsonObject& doc) {
    if (doc.value("version").toInt(0) != CHAT_DOC_VERSION) return {};
    return sanitizeChatMessages(doc.value("messages").toArray(), /*dropEmpty=*/false);
  }
}


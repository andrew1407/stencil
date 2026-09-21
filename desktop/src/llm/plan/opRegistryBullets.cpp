// Assembling the "Available ops" section of the §4 system prompt out of the registry: the
// shared-bullet dedupe, the "also accepts" addenda, and the secret-leak tooth on every bullet.
#include "opRegistry.hpp"
#include "OpSchema.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace stencil::llm {

  bool bulletLeaksSecrets(const QString& bullet) {
    // §13 censor patterns: api keys, bearer tokens, endpoint-setting
    // instructions. A match fails assembly loudly — never leaks into the prompt.
    static const QVector<QRegularExpression> patterns = {
        QRegularExpression("api[\\s_-]?key", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bbearer\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("access[\\s_-]?token", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("auth(orization)?[\\s_-]?token", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bendpoint\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("base[\\s_-]?url", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bsecret\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bpassword\\b", QRegularExpression::CaseInsensitiveOption),
    };
    for (const auto& re : patterns)
      if (re.match(bullet).hasMatch()) return true;
    return false;
  }

  QString assembleOpsBullets(const QVector<OpDescriptor>& entries,
                             const QVector<OpAddendum>& addenda, unsigned caps,
                             QString* censorError) {
    QStringList bullets;
    QStringList emitted;   // shared-bullet dedupe (undo/redo, connect/disconnect)
    QVector<OpKind> included;
    int lastEditorAt = -1;
    auto push = [&](const QString& b) -> bool {
      if (bulletLeaksSecrets(b)) {
        // §13 censor: fail loudly at assembly, drop the bullet.
        if (censorError && censorError->isEmpty())
          *censorError = QStringLiteral("op registry bullet matches a sensitive pattern");
        Q_ASSERT(censorError != nullptr);  // production callers must observe the failure
        return false;
      }
      bullets.append(b);
      return true;
    };
    for (const OpDescriptor& e : entries) {
      if ((caps & e.capability) != e.capability) continue;  // §13 capability truth
      included.append(e.kind);
      if (emitted.contains(e.bullet)) continue;
      emitted.append(e.bullet);
      if (push(e.bullet) && e.editorSettings) lastEditorAt = bullets.size() - 1;
    }
    // Addenda close the editor block (right after its last bullet).
    QStringList extra;
    for (const OpAddendum& ad : addenda) {
      if (!included.contains(ad.kind)) continue;
      const QString& b = ad.bullet;
      if (bulletLeaksSecrets(b)) {
        if (censorError && censorError->isEmpty())
          *censorError = QStringLiteral("op registry bullet matches a sensitive pattern");
        Q_ASSERT(censorError != nullptr);
        continue;
      }
      extra.append(b);
    }
    if (!extra.isEmpty() && lastEditorAt >= 0)
      for (int i = 0; i < extra.size(); ++i) bullets.insert(lastEditorAt + 1 + i, extra.at(i));
    return bullets.join(QLatin1Char('\n'));
  }

  QString assembleOpsSection(unsigned caps) {
    QString censor;
    const QString s = assembleOpsBullets(opRegistry(), opAddenda(), caps, &censor);
    Q_ASSERT(censor.isEmpty());
    return s;
  }

  QString assembleEditorOpsBlock(unsigned caps) {
    QVector<OpDescriptor> editor;
    for (const OpDescriptor& e : opRegistry())
      if (e.editorSettings) editor.append(e);
    QString censor;
    const QString s = assembleOpsBullets(editor, opAddenda(), caps, &censor);
    Q_ASSERT(censor.isEmpty());
    return s;
  }

  QString assembleSystemPrompt(unsigned caps) {
    return promptText(QStringLiteral("head")) + assembleOpsSection(caps)
           + promptText(QStringLiteral("tail"));
  }

  const QString& assembledSystemPrompt() {
    static const QString prompt = assembleSystemPrompt(CAP_ALL_DESKTOP);
    return prompt;
  }

}  // namespace stencil::llm

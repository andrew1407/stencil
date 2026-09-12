#pragma once
#include "opPlan.hpp"
#include <QString>
#include <QStringList>
#include <QVector>

// Op registry (llm-contract.md §13): the desktop's view of config/llm/opRegistry.json, the
// validator source of truth; the prompt's ops sections are ASSEMBLED from it, never hand-embedded.
namespace stencil::llm {

  // §13 "capability truth": assembling with a reduced set EXCLUDES the ops that need the missing ones.
  enum OpCapability : unsigned {
    CAP_NONE = 0u,
    CAP_CLIPBOARD = 1u << 0,
    CAP_SERVERS = 1u << 1,
    CAP_VIDEO = 1u << 2,
    CAP_FILESYSTEM = 1u << 3,
    CAP_ALL_DESKTOP = CAP_CLIPBOARD | CAP_SERVERS | CAP_VIDEO | CAP_FILESYSTEM,
  };

  // A `bullet` shared by two kinds (undo/redo, connect/disconnect) is emitted once.
  struct OpDescriptor {
    OpKind kind;
    const char* name;
    QString bullet;
    bool editorSettings;
    bool topLevelOnly;
    bool history;
    unsigned capability;
  };

  // The registry's `also` lines, emitted at the end of the §10 block; dropped with the op.
  struct OpAddendum {
    OpKind kind;
    QString bullet;
  };

  // In prompt emission order (§2 core ops, then the §10 block between frame and image).
  const QVector<OpDescriptor>& opRegistry();
  const QVector<OpAddendum>& opAddenda();

  // One string field of the shared prompt canon (config/llm/systemPrompt.json), Qt %1/%2 intact.
  QString promptText(const QString& key);

  QString opName(OpKind kind);
  bool opKindFor(const QString& name, OpKind* out);

  // §13 forbidden ops (forbidden.perSurface.desktop): the parser skips them, the executor refuses.
  QStringList forbiddenOps();
  bool isForbiddenOpName(const QString& name);
  bool rejectForbiddenOp(const QString& name, QString* err);

  // §13 prompt censor (api-key / bearer-token / endpoint patterns); assembly fails loudly.
  bool bulletLeaksSecrets(const QString& bullet);

  // A censor hit sets *censorError and drops the bullet (Q_ASSERT in dev builds).
  QString assembleOpsBullets(const QVector<OpDescriptor>& entries,
                             const QVector<OpAddendum>& addenda, unsigned caps,
                             QString* censorError = nullptr);

  QString assembleOpsSection(unsigned caps = CAP_ALL_DESKTOP);
  QString assembleEditorOpsBlock(unsigned caps = CAP_ALL_DESKTOP);
  QString assembleSystemPrompt(unsigned caps = CAP_ALL_DESKTOP);
  const QString& assembledSystemPrompt();

}  // namespace stencil::llm

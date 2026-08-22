#pragma once
#include "opPlan.hpp"
#include <QString>
#include <QStringList>
#include <QVector>

// Op registry (llm-contract.md §13) — the single source of an op's existence
// on the desktop surface. One descriptor per OpKind carries the op's wire
// name, its §4/§10 prompt bullet (verbatim), its flags, and its runtime
// capability requirement; the "Available ops" section and the §10 editor
// block of the system prompt are ASSEMBLED from this table (llmClient
// systemPrompt), never hand-embedded. Tests pin the op-name set, the flags
// and one key phrase per bullet — see tests/llmClient.headless.cpp.
namespace stencil::llm {

  // Runtime capabilities a bullet may require (§13 "capability truth"). On
  // the full desktop editor every capability is wired; assembling with a
  // reduced set EXCLUDES the ops that need the missing ones, so the prompt
  // never promises an op the surface cannot run.
  enum OpCapability : unsigned {
    CapNone = 0u,
    CapClipboard = 1u << 0,  // system clipboard (copy)
    CapServers = 1u << 1,    // saved collaboration-server stores (connect/disconnect)
    CapVideo = 1u << 2,      // video frame extraction (frame)
    CapFilesystem = 1u << 3, // reading/writing user-named local paths (openFile, save path)
    CapAllDesktop = CapClipboard | CapServers | CapVideo | CapFilesystem,
  };

  // One registry entry. `bullet` may be shared between two kinds that share
  // one prompt row (undo/redo, connect/disconnect) — assembly emits a shared
  // bullet once. `editorSettings` decides which prompt section the bullet
  // joins (§10 editor block vs the core §2 list).
  struct OpDescriptor {
    OpKind kind;
    const char* name;      // wire op name — must match the opPlan parser
    const char* bullet;    // verbatim prompt bullet (no trailing newline)
    bool editorSettings;   // §10 scope (banned in variants; editor block)
    bool topLevelOnly;     // §2/§2.1 top-level-only enforcement
    bool history;          // §2 undo/redo history ops
    unsigned capability;   // OpCapability bits required (CapNone = always)
  };

  // A widening note attached to an already-registered op ("… also accepts"),
  // emitted at the end of the §10 block; dropped with its op when the op's
  // capability is missing.
  struct OpAddendum {
    OpKind kind;
    const char* bullet;
  };

  // The full desktop table, in prompt emission order (§2 core ops, then the
  // §10 editor block between the frame and image bullets), and its addenda.
  const QVector<OpDescriptor>& opRegistry();
  const QVector<OpAddendum>& opAddenda();

  // Wire name of a kind ("" if somehow unregistered).
  QString opName(OpKind kind);

  // §13 forbidden ops — names the model must never be able to drive:
  // llm/provider configuration, clipboard reads, hotkey rebinding,
  // session/window end, chat persistence/consent toggles, and server-side
  // destruction beyond what §10 grants. Enforced by a registry test (no
  // entry may use one) and an executor-level reject (rejectForbiddenOp).
  QStringList forbiddenOps();
  bool isForbiddenOpName(const QString& name);
  // Executor tooth: true (and sets *err) when `name` is forbidden.
  bool rejectForbiddenOp(const QString& name, QString* err);

  // §13 prompt censor: does this bullet leak sensitive material (api-key /
  // bearer-token / endpoint-setting patterns)? Assembly fails loudly on it.
  bool bulletLeaksSecrets(const QString& bullet);

  // Assemble the prompt pieces from an arbitrary entry list (exposed so tests
  // can poison an entry / reduce capabilities). A censor hit sets
  // *censorError and drops the bullet (Q_ASSERT in dev builds).
  QString assembleOpsBullets(const QVector<OpDescriptor>& entries,
                             const QVector<OpAddendum>& addenda, unsigned caps,
                             QString* censorError = nullptr);

  // The §4 "Available ops" section + spliced §10 editor block for `caps`.
  QString assembleOpsSection(unsigned caps = CapAllDesktop);
  // The §10 editor block alone (theme … the lineStyle widening).
  QString assembleEditorOpsBlock(unsigned caps = CapAllDesktop);
  // The full system prompt (§4 prose core + assembled ops) for `caps`.
  QString assembleSystemPrompt(unsigned caps = CapAllDesktop);
  // Cached full-capability prompt used by LlmClient::systemPrompt.
  const QString& assembledSystemPrompt();

}  // namespace stencil::llm

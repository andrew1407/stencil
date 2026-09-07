#pragma once
#include "models.hpp"
#include <QChar>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// LLM op-plan (v1) parser — the desktop implementation of llm-contract.md
// §1–2 (mirrored by browser js/llm/opPlan.js, pystencil llm.py, the bot's
// OpPlanParser and mcp's opplan.rs; the parse matrix must stay identical).
// Semantics implemented here:
//   • Extraction tolerance: strip Markdown code fences, take the first balanced
//     { … } that parses as a JSON object. No JSON object at all ⇒ chat-only
//     (raw text becomes the reply; zero actions; NOT an error).
//   • Strict validation once an object is found: `reply` must be a non-empty
//     string; unknown fields / wrong types / out-of-range values reject an
//     action; a KNOWN op with invalid params fails the whole plan; an UNKNOWN
//     op is dropped with a warning (forward compatibility). `version` other
//     than 1 (or absent) is accepted but ignored.
//   • §1's one exception: a top-level-only or editor-settings op inside a
//     variant (or an ask-option preview) drops THAT variant/preview with a
//     warning — the top-level actions and the well-formed variants still run.
//   • Limits (same numbers everywhere): ≤16 actions per list (top-level and
//     each variant), ≤8 variants, ≤200 layout lines, ≤5000 chars per
//     formula/spec string field, ≤32 frame indices.
namespace stencil::llm {

  inline constexpr int kMaxActions = 16;
  inline constexpr int kMaxVariants = 8;
  inline constexpr int kMaxLayoutLines = 200;
  inline constexpr int kMaxSpecChars = 5000;
  // §10: the longest local path an openFile / save destination may carry.
  inline constexpr int kMaxPathChars = 1024;
  inline constexpr int kMaxFrameIndices = 32;

  // §11 interactive replies — the same numbers as every other client.
  inline constexpr int kMinAskOptions = 2;
  inline constexpr int kMaxAskOptions = 5;
  inline constexpr int kMaxAskQuestion = 300;
  inline constexpr int kMaxAskLabel = 80;
  inline constexpr int kMaxAskAnswer = 500;

  // §2 image ops + the §10 editor-settings ops (GUI editors only: Theme…
  // ClearChat adjust the EDITOR, never the image, and are banned in variants)
  // + the §2 history ops (Undo/Redo, top-level only — a sandboxed variant or
  // ask preview has no edit history) + the §2.1 multi-image ops (Image/Save,
  // top-level only).
  enum class OpKind {
    Crop, Rotate, Filter, Layout, Formula, Page, Blank, Frame,
    OpenUrl, OpenFile, Theme, Accent, LineStyle, Units, View, Clear, Connect, Disconnect, Copy,
    RemoveProject, ClearProjects,
    Compare, Zoom, RenameProject, ProjectColor, BlankColor, OpenProject, Incognito,
    ChatPanel, Dialog, ClearChat,
    Undo, Redo,
    Image, Save
  };

  // §10 ops are forbidden inside `variants` (variants exist to produce images);
  // shared by the parser (drops that variant with a warning) and the executor
  // (defensive).
  // OpenUrl/OpenFile load a NEW working image rather than adjusting a setting,
  // but the same scope applies: GUI editors only, never inside a variant.
  inline bool isEditorSettingsOp(OpKind op) {
    return op >= OpKind::OpenUrl && op <= OpKind::ClearChat;
  }

  // §2 undo/redo: top-level only too, but with their own variant-ban message
  // (sandboxed renders write history-invisible state — contract §2).
  inline bool isHistoryOp(OpKind op) {
    return op == OpKind::Undo || op == OpKind::Redo;
  }

  // §2.1 image/save ride the SAME top-level-only enforcement as the settings ops
  // (rejected inside variants and ask-option previews); only the message differs.
  // Browser twin: opPlan.js TOP_LEVEL_ONLY_OPS.
  inline bool isTopLevelOnlyOp(OpKind op) { return op >= OpKind::OpenUrl; }

  // One validated action (tagged union on `op`; only the fields for that op are
  // meaningful). Field semantics per contract §2.
  struct Action {
    OpKind op = OpKind::Crop;
    // crop: cropSpec edge tokens ("" = edge absent; at least one spec key present).
    QString x1, x2, y1, y2;
    // crop: optional "W:H" aspect ratio (strict positive integers; "" = absent).
    // Accepted inside the spec or (§3.2 tolerance) at the action level, folded
    // here. Resolved client-side by core resolveCropRect — never re-derived.
    QString aspect;
    // rotate
    bool rotateLeft = false;
    int times = 1;  // 1..3
    // filter (`mode` doubles as the theme op's "light"|"dark")
    QString mode;  // none|bw|sepia|invert|contour|custom
    QString tint;  // "#rrggbb", present iff mode == custom
    // layout (defaults already applied per contract §3)
    core::Lines lines;
    // formula
    QChar axis;    // 'x' | 'y'
    QString expr;
    // formula: the `enabled` form (-1 = absent, else 0/1; when set, axis/expr
    // are absent — contract §2 "bool alone")
    int formulaEnabled = -1;
    // page / blank
    QString format;  // lowercase ISO name ("a4"); may be empty for blank
    QString color;   // blank fill / accent hex / lineStyle colour ("" = absent,
                     // except projectColor where "" is the explicit clear)
    // page / blank custom dims in cm (0 = absent; validated 0.1..500)
    double widthCm = 0;
    double heightCm = 0;
    // frame
    QVector<int> indices;
    // §2 undo / redo (validated 1..20, default 1)
    int steps = 1;
    // §10 lineStyle (0 = field absent; validated 1..20 / 1..30)
    int thickness = 0;
    int pointSize = 0;
    QString style;  // solid|dashed|dotted ("" = absent)
    // §10 lineStyle widening: pointColor ("" = follow the stroke — a MEANINGFUL
    // value, so a presence flag rides along), drawMode, fillColor.
    QString pointColor;
    bool pointColorSet = false;
    QString drawMode;   // "line" | "rect" ("" = absent)
    QString fillColor;  // "#rrggbb" | "transparent" ("" = absent)
    // §10 units
    QString value;  // "cm" | "in"
    // §10 compare (`mode` reuses the field above); split 0 = absent
    double split = 0;
    // §10 zoom (exactly one of the two)
    int percent = 0;
    bool fit = false;
    // §10 removeProject's `current: true` form
    bool current = false;
    // §10 copy: "" = "image" (the default)
    QString what;
    // §10 accent's preset form ("" = the hex form in `color`)
    QString preset;
    // §10 view (-1 = field absent, else 0/1)
    int viewPoints = -1;
    int viewLines = -1;
    // §10 chatPanel: the assistant panel's own placement. chatOpen -1 = field
    // absent, else 0/1; dock "" = absent (left|right|top|bottom|float).
    int chatOpen = -1;
    QString dock;
    // §10 dialog: which editor window to put in front of the user
    // (projects|servers|shortcuts|visuals|help); "" with `current` = close the open one.
    QString dialog;
    // §10 connect / disconnect: the user-visible server reference (resolved
    // against the SAVED/live connection stores — plans never carry tokens)
    QString server;
    // §10 openUrl: the user-echoed image URL + the incognito flag
    QString url;
    bool incognito = false;
    // §10 openFile: a user-echoed LOCAL path (image/video, .json layout, .stencil
    // project). §2.1 save reuses it as an optional destination folder/file.
    QString path;
    // §2.1 image: which of THIS turn's attachments to work on (1-based).
    int index = 0;
    // §2.1 save: the project name ("" = derive it from the loaded attachment,
    // else the editor's own name). §10 removeProject reuses it (trimmed,
    // required non-empty there).
    QString name;
  };

  struct Variant {
    QString label;  // may be empty; sanitize with sanitizeLabel() for naming
    QVector<Action> actions;
  };

  // ── §11 interactive replies ──
  // One choice on an `ask` card. An option either previews a render (`actions`, applied to a
  // COPY of the working image) or names an existing image (`imageUrl` / `projectId`), never
  // both; with neither it is a plain text choice.
  struct AskOption {
    QString label;
    QVector<Action> actions;  // empty unless the option previews a render
    QString imageUrl;         // http(s) only; empty when the option names none
    QString projectId;        // a stored project, resolved by the caller
  };

  // A question put back to the user, rendered as a radio/checkbox card under the reply. The
  // answer is sent as the user's NEXT turn; nothing is applied when an option is picked.
  struct AskCard {
    QString question;
    bool multi = false;
    bool allowCustom = false;
    QString customLabel;
    QVector<AskOption> options;
  };

  struct OpPlan {
    QString reply;
    QVector<Action> actions;
    QVector<Variant> variants;
    // The turn's question, when it asked one (options empty = no card).
    AskCard ask;
    // Unknown-op skip notices, to be appended to the chat reply by the caller.
    QStringList warnings;
    // True when the text held no JSON object: `reply` is the raw text, actions
    // and variants are empty.
    bool chatOnly = false;
  };

  struct OpPlanResult {
    bool ok = false;
    OpPlan plan;
    QString error;  // set when !ok (nothing executes; shown in chat)
  };

  OpPlanResult parseOpPlan(const QString& text);

  // Variant label → a safe fragment for file/project naming: keep letters,
  // digits, space, '-', '_'; collapse whitespace; cap length. Empty when
  // nothing survives.
  QString sanitizeLabel(const QString& label);

  // The text an answered card sends as the user's next turn (§11.3): the picked labels
  // joined by ", ", or the typed custom text; trimmed and capped at kMaxAskAnswer.
  QString askAnswerText(const QStringList& pickedLabels, const QString& custom = QString());

  // Does this plan actually WORK ON the picture? Only then is an attached image worth
  // adopting as the working one — a plan that only answers a question about it (or
  // changes a setting) leaves the canvas alone. Variants count: they are alternatives
  // OF the working image. Browser twin: chatController.js planEditsTheImage.
  inline bool planTouchesTheImage(const OpPlan& plan) {
    if (!plan.variants.isEmpty()) return true;
    for (const Action& a : plan.actions) {
      switch (a.op) {
        case OpKind::Crop:
        case OpKind::Rotate:
        case OpKind::Filter:
        case OpKind::Layout:
        case OpKind::Formula:
        case OpKind::Page:
        case OpKind::Blank:
        case OpKind::Frame:
        case OpKind::Clear:
          return true;
        default:
          break;   // a settings op (§10) leaves the picture alone
      }
    }
    return false;
  }

}  // namespace stencil::llm

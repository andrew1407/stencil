#pragma once
#include "models.hpp"
#include <QChar>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// LLM op-plan parser, llm-contract.md §1–2; twins: browser js/llm/opPlan.js, pystencil llm.py,
// the bot's OpPlanParser, mcp's opplan.rs — the parse matrix must stay identical. Limits come
// from the registry (opSchema.hpp); an UNKNOWN op is dropped with a warning, a known bad one fails.
namespace stencil::llm {

  // §2 image ops + §10 editor-settings ops (banned in variants) + §2 history + §2.1 multi-image.
  enum class OpKind {
    Crop, Rotate, Filter, Layout, Formula, Page, Blank, Frame,
    OpenUrl, OpenFile, Theme, Accent, LineStyle, Units, View, Clear, Connect, Disconnect, Copy,
    RemoveProject, ClearProjects,
    Compare, Zoom, RenameProject, ProjectColor, BlankColor, OpenProject, Incognito,
    ChatPanel, Dialog, ClearChat,
    Undo, Redo,
    Image, Save
  };

  // §10 ops (and OpenUrl/OpenFile) are forbidden inside `variants`: parser drops the variant.
  inline bool isEditorSettingsOp(OpKind op) {
    return op >= OpKind::OpenUrl && op <= OpKind::ClearChat;
  }

  // §2 undo/redo: top-level only, with their own variant-ban message.
  inline bool isHistoryOp(OpKind op) {
    return op == OpKind::Undo || op == OpKind::Redo;
  }

  // Browser twin: opPlan.js TOP_LEVEL_ONLY_OPS.
  inline bool isTopLevelOnlyOp(OpKind op) { return op >= OpKind::OpenUrl; }

  // Tagged union on `op`; field semantics per contract §2.
  struct Action {
    OpKind op = OpKind::Crop;
    // crop: "" = edge absent
    QString x1, x2, y1, y2;
    // crop: "W:H"; accepted in the spec or (§3.2) at the action level; resolved by core resolveCropRect.
    QString aspect;
    bool rotateLeft = false;
    int times = 1;  // 1..3
    // filter (`mode` doubles as the theme op's "light"|"dark")
    QString mode;  // none|bw|sepia|invert|contour|custom
    QString tint;  // "#rrggbb", present iff mode == custom
    core::Lines lines;
    QChar axis;    // 'x' | 'y'
    QString expr;
    // formula `enabled` form (-1 = absent; when set, axis/expr are absent — §2 "bool alone")
    int formulaEnabled = -1;
    QString format;
    QString color;   // blank fill / accent hex / lineStyle colour ("" = absent,
                     // ("" = absent, except projectColor where "" is the explicit clear)
    // cm (0 = absent; validated 0.1..500)
    double widthCm = 0;
    double heightCm = 0;
    QVector<int> indices;
    // undo/redo (validated 1..20)
    int steps = 1;
    // lineStyle (0 = absent; validated 1..20 / 1..30)
    int thickness = 0;
    int pointSize = 0;
    QString style;  // solid|dashed|dotted ("" = absent)
    // pointColor "" = follow the stroke, a MEANINGFUL value, so a presence flag rides along
    QString pointColor;
    bool pointColorSet = false;
    QString drawMode;   // "line" | "rect" ("" = absent)
    QString fillColor;  // "#rrggbb" | "transparent" ("" = absent)
    QString value;  // "cm" | "in"
    // compare (`mode` reused); split 0 = absent
    double split = 0;
    // zoom (exactly one of the two)
    int percent = 0;
    bool fit = false;
    bool current = false;
    // copy: "" = "image"
    QString what;
    // accent preset form ("" = the hex form in `color`)
    QString preset;
    // view (-1 = absent, else 0/1)
    int viewPoints = -1;
    int viewLines = -1;
    // chatPanel: chatOpen -1 = absent; dock "" = absent
    int chatOpen = -1;
    QString dock;
    // dialog: "" with `current` = close the open one
    QString dialog;
    // connect/disconnect: resolved against the connection stores — plans never carry tokens
    QString server;
    QString url;
    bool incognito = false;
    // openFile: a user-echoed LOCAL path; §2.1 save reuses it as the destination
    QString path;
    // §2.1 image: 1-based attachment index
    int index = 0;
    // §2.1 save name ("" = derived); §10 removeProject reuses it
    QString name;
  };

  struct Variant {
    QString label;
    QVector<Action> actions;
  };

  // §11: an option previews a render (`actions`) OR names an image, never both.
  struct AskOption {
    QString label;
    QVector<Action> actions;
    QString imageUrl;
    QString projectId;
  };

  // §11: the answer is sent as the user's NEXT turn; nothing is applied on pick.
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
    // options empty = no card
    AskCard ask;
    QStringList warnings;
    // No JSON object: `reply` is the raw text, actions and variants are empty.
    bool chatOnly = false;
  };

  struct OpPlanResult {
    bool ok = false;
    OpPlan plan;
    QString error;
  };

  OpPlanResult parseOpPlan(const QString& text);

  // Keeps letters, digits, space, '-', '_'; collapses whitespace; caps length.
  QString sanitizeLabel(const QString& label);

  // §11.3: picked labels joined by ", ", or the custom text; capped at ask.answer.
  QString askAnswerText(const QStringList& pickedLabels, const QString& custom = QString());

  // Only a plan that WORKS ON the picture adopts an attachment as the working image;
  // variants count. Browser twin: chatController.js planEditsTheImage.
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
          break;
      }
    }
    return false;
  }

}  // namespace stencil::llm

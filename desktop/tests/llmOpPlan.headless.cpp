// Headless check of the LLM op-plan parser (src/llm/opPlan) — the desktop port
// of the llm-contract.md §1-2 parse matrix (shared with every client in
// the contract table): extraction tolerance (fence stripping, first balanced
// object, chat-only fallback), strict known-op validation, unknown-op
// skip-with-warning, and the shared limits (16 actions / 8 variants / 200
// lines / 5000 chars / 32 frame indices). Pure QtCore; no display needed.
#include "opPlan.hpp"
#include "OpSchema.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <cstdio>

using namespace stencil::llm;

#include "support/check.hpp"

// §1's one exception: a top-level-only op inside a variant drops THAT variant
// with a warning — the plan itself survives.
static bool variantDropped(const char* json) {
  const auto r = parseOpPlan(QString::fromUtf8(json));
  return r.ok && r.plan.variants.isEmpty() && r.plan.warnings.size() == 1 &&
         r.plan.warnings[0].startsWith("Dropped variant");
}

// The same for an ask-option preview: the option stays, its preview goes.
static bool previewDropped(const char* json) {
  const auto r = parseOpPlan(QString::fromUtf8(json));
  return r.ok && !r.plan.ask.options.isEmpty() &&
         r.plan.ask.options[0].actions.isEmpty() && r.plan.warnings.size() == 1 &&
         r.plan.warnings[0].contains("preview for option");
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ── extraction tolerance ──
  std::printf("extraction:\n");
  {
    const auto r = parseOpPlan("Just chatting, no JSON here.");
    check(r.ok && r.plan.chatOnly, "plain text -> chat-only, not an error");
    check(r.plan.reply == "Just chatting, no JSON here.", "chat-only reply is the raw text");
    check(r.plan.actions.isEmpty() && r.plan.variants.isEmpty(), "chat-only has no actions");
  }
  {
    const auto r = parseOpPlan(
        "```json\n{\"version\":1,\"reply\":\"ok\",\"actions\":[]}\n```");
    check(r.ok && !r.plan.chatOnly && r.plan.reply == "ok", "fenced JSON parses");
  }
  {
    const auto r = parseOpPlan(
        "Sure! {\"version\":1,\"reply\":\"done\",\"actions\":[{\"op\":\"rotate\","
        "\"dir\":\"left\"}]} hope that helps");
    check(r.ok && r.plan.actions.size() == 1, "first balanced object amid prose");
    check(r.plan.actions[0].op == OpKind::ROTATE && r.plan.actions[0].rotateLeft &&
              r.plan.actions[0].times == 1,
          "rotate defaults times=1");
  }
  {
    // Balanced braces that aren't JSON are skipped; the real object still found.
    const auto r = parseOpPlan("The set {a, b} maps to {\"reply\":\"found\"}");
    check(r.ok && !r.plan.chatOnly && r.plan.reply == "found",
          "non-JSON balanced braces skipped");
  }
  {
    // Braces inside the reply string don't break the brace counter.
    const auto r = parseOpPlan("{\"reply\":\"use {x1} tokens\",\"actions\":[]}");
    check(r.ok && r.plan.reply == "use {x1} tokens", "braces inside strings ignored");
  }
  {
    const auto r = parseOpPlan("{\"version\":2,\"reply\":\"ok\"}");
    check(r.ok && r.plan.reply == "ok", "version != 1 accepted but ignored");
  }

  // ── strict top-level validation ──
  std::printf("top-level:\n");
  {
    // §1 reply tolerance: "Done." + a warning, the plan itself survives.
    const auto r = parseOpPlan(
        "{\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}");
    check(r.ok && r.plan.reply == "Done." && r.plan.actions.size() == 1 &&
              r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("omitted its reply"),
          "missing reply tolerated: Done. + warning, actions kept");
  }
  {
    // An EMPTY plan says so — a bare "Done." would read as a success that
    // never occurred (contract §1).
    const auto r = parseOpPlan("{\"reply\":\"   \"}");
    check(r.ok && r.plan.reply.contains("empty plan") && r.plan.warnings.isEmpty(),
          "blank reply on an empty plan: says nothing changed, no \"it ran\" claim");
  }
  {
    const auto r = parseOpPlan("{\"reply\":\"x\",\"actions\":{}}");
    check(!r.ok, "non-array actions fails the plan");
  }
  {
    QString many = "{\"reply\":\"x\",\"actions\":[";
    for (int i = 0; i < 17; ++i)
      many += QString("%1{\"op\":\"rotate\",\"dir\":\"left\"}").arg(i ? "," : "");
    many += "]}";
    check(!parseOpPlan(many).ok, "17 actions exceed the 16 limit");
  }
  check(!parseOpPlan("{\"reply\":\"x\",\"variants\":[{\"label\":7,\"actions\":[]}]}").ok,
        "a non-string variant label fails the plan (registry envelope)");
  check(parseOpPlan("{\"reply\":\"x\",\"variants\":[{\"label\":\"v\",\"actions\":[],\"note\":\"x\"}]}").ok,
        "a variant object tolerates undeclared keys (envelope allowUnknown)");
  {
    QString many = "{\"reply\":\"x\",\"variants\":[";
    for (int i = 0; i < 9; ++i) many += QString("%1{\"label\":\"v\"}").arg(i ? "," : "");
    many += "]}";
    check(!parseOpPlan(many).ok, "9 variants exceed the 8 limit");
  }
  {
    // Unknown op: dropped with a warning; the rest of the plan stands.
    const auto r = parseOpPlan(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"resize\",\"w\":10},"
        "{\"op\":\"rotate\",\"dir\":\"right\",\"times\":2}]}");
    check(r.ok, "unknown op does not fail the plan");
    check(r.plan.actions.size() == 1 && r.plan.actions[0].times == 2,
          "known action kept after unknown-op skip");
    check(r.plan.warnings.size() == 1 && r.plan.warnings[0].contains("resize"),
          "unknown op leaves a warning");
  }

  // ── crop ──
  std::printf("crop:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"x1\":\"10%\",\"x2\":\"-10%\",\"y1\":\"0\",\"y2\":\"9.5cm\"}}]}");
    check(r.ok && r.plan.actions.size() == 1, "crop with %/bare/cm tokens parses");
    check(r.plan.actions[0].x1 == "10%" && r.plan.actions[0].y2 == "9.5cm",
          "crop tokens preserved");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"  10%  \"}}]}");
    check(r.ok && r.plan.actions[0].x1 == "10%", "crop tokens are trimmed before validation");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{}}]}").ok,
        "empty crop spec fails");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"z\":\"1\"}}]}")
             .ok,
        "unknown crop spec key fails");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"10mm\"}}]}")
             .ok,
        "mm unit rejected (contract allows % px cm in)");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"abc\"}}]}")
             .ok,
        "non-numeric crop token fails");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"1px\"},\"extra\":1}]}")
             .ok,
        "unknown field on a crop action fails");
  {
    // The aspect key — strict W:H, digits only, both positive (browser
    // opPlan.test.js parity), passed through intact for core resolveCropRect.
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"x1\":\"10%\",\"aspect\":\"4:3\"}}]}");
    check(r.ok && r.plan.actions.size() == 1, "crop with an aspect key parses");
    check(r.plan.actions[0].x1 == "10%" && r.plan.actions[0].aspect == "4:3",
          "edge token and aspect both preserved for the executor");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"1:1\"}}]}");
    check(r.ok && r.plan.actions[0].aspect == "1:1" && r.plan.actions[0].x1.isEmpty(),
          "aspect alone satisfies the at-least-one-key rule");
  }
  check(parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                    "{\"aspect\":\"16:9\"}}]}")
            .ok,
        "16:9 aspect parses");
  for (const char* bad : {"0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2", "4",
                          "4:", ":3", "1e2:3", "", " 4:3 "}) {
    check(!parseOpPlan(QString("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                               "{\"aspect\":\"%1\"}}]}")
                           .arg(bad))
               .ok,
          qPrintable(QString("malformed aspect \"%1\" fails the whole plan").arg(bad)));
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"aspect\":43}}]}")
             .ok,
        "non-string aspect fails");
  {
    // §3.2 action-level tolerance: "aspect" beside "spec" is accepted and
    // folds into the spec — same strict W:H rule as the canonical spelling.
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"x1\":\"10%\"},\"aspect\":\"4:3\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].x1 == "10%" &&
              r.plan.actions[0].aspect == "4:3",
          "action-level aspect folds into the spec");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{},"
        "\"aspect\":\"1:1\"}]}");
    check(r.ok && r.plan.actions[0].aspect == "1:1" && r.plan.actions[0].x1.isEmpty(),
          "a folded action-level aspect satisfies the at-least-one-key rule");
  }
  check(!parseOpPlan(
             "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"aspect\":\"1:1\"}]}")
             .ok,
        "action-level aspect never replaces the spec object itself");
  for (const char* bad : {"0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2", "4",
                          "4:", ":3", "1e2:3", "", " 4:3 "}) {
    check(!parseOpPlan(QString("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                               "{\"x1\":\"10%\"},\"aspect\":\"%1\"}]}")
                           .arg(bad))
               .ok,
          qPrintable(
              QString("malformed action-level aspect \"%1\" fails the whole plan").arg(bad)));
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"10%\"},\"aspect\":43}]}")
             .ok,
        "non-string action-level aspect fails");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"aspect\":\"4:3\"},\"aspect\":\"4:3\"}]}");
    check(r.ok && r.plan.actions[0].aspect == "4:3",
          "agreeing duplicate aspect keys are tolerated (spec kept)");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"aspect\":\"4:3\"},\"aspect\":\"16:9\"}]}")
             .ok,
        "conflicting duplicate aspect keys fail the whole plan");

  // ── rotate ──
  std::printf("rotate:\n");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"up\"}]}").ok,
        "bad rotate dir fails");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\","
                     "\"times\":0}]}")
             .ok,
        "times 0 out of range");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\","
                     "\"times\":4}]}")
             .ok,
        "times 4 out of range");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\","
                     "\"times\":1.5}]}")
             .ok,
        "fractional times fails");

  // ── filter ──
  std::printf("filter:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\","
        "\"tint\":\"#A1b2C3\"}]}");
    check(r.ok && r.plan.actions[0].tint == "#A1b2C3", "custom filter with hex tint");
  }
  check(parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"contour\"}]}").ok,
        "contour filter parses");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\"}]}").ok,
        "custom without tint fails");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\","
                     "\"tint\":\"#112233\"}]}")
             .ok,
        "tint forbidden unless custom");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\","
                     "\"tint\":\"#12z\"}]}")
             .ok,
        "malformed tint hex fails");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"blur\"}]}").ok,
        "unknown filter mode fails");

  // ── layout ──
  std::printf("layout:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":"
        "[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}]}]}]}");
    check(r.ok && r.plan.actions[0].lines.size() == 1, "minimal line parses");
    const auto& line = r.plan.actions[0].lines[0];
    check(line.color == "#FFFF00" && line.thickness == 2.0 && line.pointSize == 4.0 &&
              line.style == "solid" && !line.locked && line.fillColor == "transparent",
          "per-line defaults applied (contract §3)");
  }
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{"
                     "\"points\":[{\"x\":1,\"y\":2}],\"style\":\"wavy\"}]}]}")
             .ok,
        "unknown line style fails");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{"
                     "\"points\":[{\"x\":1,\"y\":2}],\"width\":3}]}]}")
             .ok,
        "unknown line field fails");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{"
                     "\"points\":[]}]}]}")
             .ok,
        "empty points fails");
  {
    // Valid but draws nothing — the §7 shape test must not count it as "drew".
    const auto r =
        parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[]}]}");
    check(r.ok && r.plan.actions[0].lines.empty(), "empty lines array parses (zero lines)");
  }
  {
    QString lines;
    for (int i = 0; i < 201; ++i)
      lines += QString("%1{\"points\":[{\"x\":1,\"y\":2}]}").arg(i ? "," : "");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[" +
                       lines + "]}]}")
               .ok,
          "201 lines exceed the 200 limit");
  }

  // ── formula ──
  std::printf("formula:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\","
        "\"expr\":\"x**2 + 10\"}]}");
    check(r.ok && r.plan.actions[0].axis == QChar('x'), "x-axis formula parses (** allowed)");
  }
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\","
                     "\"expr\":\"y*2\"}]}")
             .ok,
        "variable must match the axis");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"y\","
                     "\"expr\":\"y^2\"}]}")
             .ok,
        "charset rejects ^");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"z\","
                     "\"expr\":\"1\"}]}")
             .ok,
        "axis must be x or y");
  {
    const QString big(5001, QChar('1'));
    check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\","
                       "\"expr\":\"" + big + "\"}]}")
               .ok,
          "5001-char expr exceeds the 5000 limit");
  }

  {
    // §2: an empty expr CLEARS that axis (identity) — no longer a parse error.
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"y\",\"expr\":\"\"}]}");
    check(r.ok && r.plan.actions[0].axis == QChar('y') && r.plan.actions[0].expr.isEmpty(),
          "empty expr parses as a clear of that axis");
  }
  {
    // §2: `enabled` is a bool ALONE — false switches formulas OFF.
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"enabled\":false}]}");
    check(r.ok && r.plan.actions[0].formulaEnabled == 0, "enabled:false parses alone");
    const auto on = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"enabled\":true}]}");
    check(on.ok && on.plan.actions[0].formulaEnabled == 1, "enabled:true parses alone");
    check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\","
                       "\"enabled\":false,\"axis\":\"x\",\"expr\":\"x\"}]}")
               .ok,
          "enabled combined with axis/expr fails");
    check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\","
                       "\"enabled\":1}]}")
               .ok,
          "non-boolean enabled fails");
  }

  // ── page / blank ──
  std::printf("page/blank:\n");
  check(parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"a4\"}]}").ok,
        "page a4 parses");
  check(parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"c10\"}]}").ok,
        "page c10 parses");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"A4\"}]}").ok,
        "uppercase format rejected (contract: lowercase)");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"a11\"}]}").ok,
        "a11 out of range");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"d4\"}]}").ok,
        "d series rejected");
  check(parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\","
                    "\"format\":\"a4\"}]}")
            .ok,
        "blank with hex + format parses");
  check(parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"red\"}]}").ok,
        "blank with CSS colour name parses");
  check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"#fff\"}]}").ok,
        "#fff shorthand rejected (contract: #rrggbb)");
  check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\","
                     "\"color\":\"notacolor\"}]}")
             .ok,
        "unknown colour name fails");
  {
    // §2: page takes format OR custom cm dims — exactly one form.
    const auto r = parseOpPlan(
        "{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"width\":20,\"height\":30}]}");
    check(r.ok && r.plan.actions[0].widthCm == 20.0 && r.plan.actions[0].heightCm == 30.0,
          "custom page dims parse (cm)");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\"}]}").ok,
          "page with neither form fails");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\","
                       "\"format\":\"a4\",\"width\":20,\"height\":30}]}")
               .ok,
          "page with both forms fails");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"width\":20}]}").ok,
          "page width without height fails");
    for (const char* bad : {"0.05", "501", "-3", "0"}) {
      check(!parseOpPlan(QString("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\","
                                 "\"width\":%1,\"height\":10}]}")
                             .arg(bad))
                 .ok,
            qPrintable(QString("page dim %1 out of 0.1..500 fails").arg(bad)));
    }
  }
  {
    // §2: blank's optional cm dims — both or neither, overriding format.
    const auto r = parseOpPlan(
        "{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\","
        "\"format\":\"a4\",\"width\":10,\"height\":5}]}");
    check(r.ok && r.plan.actions[0].widthCm == 10.0 && r.plan.actions[0].heightCm == 5.0 &&
              r.plan.actions[0].format == "a4",
          "blank dims ride beside the format (dims win at execution)");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\","
                       "\"color\":\"#ffffff\",\"width\":10}]}")
               .ok,
          "blank width without height fails");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\","
                       "\"color\":\"#ffffff\",\"width\":10,\"height\":900}]}")
               .ok,
          "blank dim out of 0.1..500 fails");
  }

  // ── §2 undo / redo ──
  std::printf("undo/redo (s2):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"u\",\"actions\":[{\"op\":\"undo\"},{\"op\":\"redo\",\"steps\":20}]}");
    check(r.ok && r.plan.actions.size() == 2, "undo + redo parse");
    check(r.plan.actions[0].op == OpKind::UNDO && r.plan.actions[0].steps == 1,
          "undo defaults steps=1");
    check(r.plan.actions[1].op == OpKind::REDO && r.plan.actions[1].steps == 20,
          "redo keeps its steps");
  }
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"undo\",\"steps\":0}]}").ok,
        "undo steps 0 out of range (1..20)");
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"redo\",\"steps\":21}]}").ok,
        "redo steps 21 out of range");
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"undo\",\"all\":true}]}").ok,
        "unknown undo field fails");
  {
    // Top-level only: history is invisible inside a sandboxed variant/preview —
    // §1 drops that variant/preview with a warning, never the whole plan.
    const auto v = parseOpPlan(
        "{\"reply\":\"u\",\"variants\":[{\"label\":\"v\",\"actions\":[{\"op\":\"undo\"}]}]}");
    check(v.ok && v.plan.variants.isEmpty(), "undo inside a variant drops the variant");
    check(v.plan.warnings.size() == 1 &&
              v.plan.warnings[0].contains("Dropped variant \"v\"") &&
              v.plan.warnings[0].contains("history"),
          "…named, with its own history wording");
    const auto p = parseOpPlan(
        "{\"reply\":\"u\",\"ask\":{\"question\":\"Q\",\"options\":["
        "{\"label\":\"A\",\"actions\":[{\"op\":\"redo\"}]},{\"label\":\"B\"}]}}");
    check(p.ok && p.plan.ask.options.size() == 2 && p.plan.ask.options[0].actions.isEmpty(),
          "redo inside an ask-option preview drops the preview, keeps the option");
    check(isTopLevelOnlyOp(OpKind::UNDO) && isTopLevelOnlyOp(OpKind::REDO) &&
              !isEditorSettingsOp(OpKind::UNDO) && !isEditorSettingsOp(OpKind::REDO),
          "undo/redo are top-level only without being editor-settings ops");
  }
  {
    // §2 reset: editors have no single reset control — the op stays UNKNOWN
    // here and is skipped with a warning per §1.
    const auto r = parseOpPlan(
        "{\"reply\":\"r\",\"actions\":[{\"op\":\"reset\"},"
        "{\"op\":\"rotate\",\"dir\":\"left\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::ROTATE,
          "reset is dropped as an unknown op; the rest of the plan stands");
    check(r.plan.warnings.size() == 1 && r.plan.warnings[0].contains("reset"),
          "…with the unknown-op warning naming it");
  }

  // ── frame ──
  std::printf("frame:\n");
  {
    const auto r =
        parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"index\":7}]}");
    check(r.ok && r.plan.actions[0].indices == QVector<int>{7}, "frame index parses");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"indices\":[0,30,60]}]}");
    check(r.ok && r.plan.actions[0].indices == (QVector<int>{0, 30, 60}),
          "frame indices parse");
  }
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"index\":1,"
                     "\"indices\":[2]}]}")
             .ok,
        "both index and indices fails");
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\"}]}").ok,
        "neither index nor indices fails");
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"index\":-1}]}").ok,
        "negative index fails");
  {
    QString idx;
    for (int i = 0; i < 33; ++i) idx += QString("%1%2").arg(i ? "," : "").arg(i);
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"indices\":[" +
                       idx + "]}]}")
               .ok,
          "33 indices exceed the 32 limit");
  }

  // ── §10 editor-settings ops (GUI editors) ──
  std::printf("editor settings (s10):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"t\",\"actions\":[{\"op\":\"theme\",\"mode\":\"dark\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::THEME &&
              r.plan.actions[0].mode == "dark",
          "theme dark parses");
  }
  check(!parseOpPlan("{\"reply\":\"t\",\"actions\":[{\"op\":\"theme\",\"mode\":\"blue\"}]}").ok,
        "theme mode must be light|dark");
  check(!parseOpPlan("{\"reply\":\"t\",\"actions\":[{\"op\":\"theme\",\"mode\":\"dark\","
                     "\"x\":1}]}")
             .ok,
        "unknown theme field fails");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\",\"color\":\"#7c3aed\"}]}");
    check(r.ok && r.plan.actions[0].color == "#7c3aed", "accent hex parses");
  }
  check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\",\"color\":\"red\"}]}").ok,
        "accent requires #rrggbb (no names)");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\",\"color\":\"lime\","
        "\"thickness\":3,\"pointSize\":6,\"style\":\"dashed\"}]}");
    check(r.ok && r.plan.actions[0].color == "lime" && r.plan.actions[0].thickness == 3 &&
              r.plan.actions[0].pointSize == 6 && r.plan.actions[0].style == "dashed",
          "full lineStyle parses (CSS colour name ok)");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\",\"thickness\":20}]}");
    check(r.ok && r.plan.actions[0].thickness == 20 && r.plan.actions[0].color.isEmpty(),
          "single-field lineStyle subset parses");
  }
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\"}]}").ok,
        "empty lineStyle fails (needs at least one field)");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                     "\"thickness\":21}]}")
             .ok,
        "thickness 21 out of range (1..20)");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                     "\"pointSize\":0}]}")
             .ok,
        "pointSize 0 out of range (1..30)");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                     "\"style\":\"wavy\"}]}")
             .ok,
        "unknown lineStyle style fails");
  check(parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"units\",\"value\":\"in\"}]}").ok,
        "units in parses");
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"units\",\"value\":\"px\"}]}").ok,
        "units px rejected (cm|in)");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[{\"op\":\"view\",\"points\":true}]}");
    check(r.ok && r.plan.actions[0].viewPoints == 1 && r.plan.actions[0].viewLines == -1,
          "view points-only parses (lines untouched)");
  }
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"view\"}]}").ok,
        "empty view fails (needs at least one field)");
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"view\",\"points\":1}]}").ok,
        "non-boolean view field fails");
  // §10 clear — "remove the image" means remove, not a white page from `blank`.
  {
    const auto r = parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"clear\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::CLEAR,
          "clear parses with no fields");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"clear\",\"color\":\"#fff\"}]}").ok,
        "clear takes no fields");
  check(variantDropped("{\"reply\":\"c\",\"actions\":[],\"variants\":[{\"label\":\"v\","
                       "\"actions\":[{\"op\":\"clear\"}]}]}"),
        "clear inside a variant drops it (variants exist to produce images)");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"connect\","
        "\"server\":\"stencil.example.com\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::CONNECT &&
              r.plan.actions[0].server == "stencil.example.com",
          "connect server reference parses");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"connect\"}]}").ok,
        "connect without server fails");
  {
    // §10 openUrl: http(s) URL + optional incognito; junk fails; variant-banned.
    const auto r = parseOpPlan(
        "{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\","
        "\"url\":\"https://a.com/cat.jpg\",\"incognito\":true}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::OPEN_URL &&
              r.plan.actions[0].url == "https://a.com/cat.jpg" &&
              r.plan.actions[0].incognito,
          "openUrl parses with incognito");
    check(!parseOpPlan("{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\"}]}").ok,
          "openUrl without url fails");
    check(!parseOpPlan("{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\","
                       "\"url\":\"ftp://a.com/x\"}]}")
               .ok,
          "non-http(s) openUrl fails");
    check(!parseOpPlan("{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\","
                       "\"url\":\"https://a.com/x\",\"tab\":1}]}")
               .ok,
          "unknown openUrl field fails");
    check(variantDropped("{\"reply\":\"o\",\"variants\":[{\"label\":\"v\",\"actions\":["
                         "{\"op\":\"openUrl\",\"url\":\"https://a.com/x\"}]}]}"),
          "openUrl inside a variant drops the variant");
  }
  // §10 copy — fieldless like clear; the working-image gate is the executor's.
  {
    const auto r = parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::COPY,
          "copy parses with no fields");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\",\"target\":\"x\"}]}").ok,
        "copy takes no fields");
  check(variantDropped("{\"reply\":\"c\",\"variants\":[{\"label\":\"v\","
                       "\"actions\":[{\"op\":\"copy\"}]}]}"),
        "copy inside a variant drops it (variants exist to produce images)");
  // Ask-option previews are rendered, never executed: same scope, same drop.
  check(previewDropped("{\"reply\":\"c\",\"ask\":{\"question\":\"q?\",\"options\":["
                       "{\"label\":\"a\",\"actions\":[{\"op\":\"copy\"}]},{\"label\":\"b\"}]}}"),
        "copy inside an ask-option preview drops the preview");
  // §10 project management — removeProject/clearProjects shapes; both are
  // editor-settings scoped, so the variant/ask-preview ban applies.
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\",\"name\":\" portrait 1 \"},"
        "{\"op\":\"clearProjects\"}]}");
    check(r.ok && r.plan.actions.size() == 2 &&
              r.plan.actions[0].op == OpKind::REMOVE_PROJECT &&
              r.plan.actions[0].name == "portrait 1" &&
              r.plan.actions[1].op == OpKind::CLEAR_PROJECTS,
          "removeProject (name trimmed) + clearProjects parse");
  }
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\"}]}").ok,
        "removeProject without a name fails");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                     "\"name\":\"   \"}]}").ok,
        "blank removeProject name fails");
  check(!parseOpPlan(QStringLiteral("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                                    "\"name\":\"%1\"}]}")
                         .arg(QString(121, QLatin1Char('a')))).ok,
        "a removeProject name over 120 chars fails");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                     "\"name\":\"x\",\"id\":\"y\"}]}").ok,
        "unknown removeProject field fails");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"clearProjects\","
                     "\"name\":\"x\"}]}").ok,
        "clearProjects takes no fields");
  check(variantDropped("{\"reply\":\"p\",\"variants\":[{\"label\":\"v\",\"actions\":["
                       "{\"op\":\"removeProject\",\"name\":\"x\"}]}]}"),
        "removeProject inside a variant drops it");
  check(variantDropped("{\"reply\":\"p\",\"variants\":[{\"label\":\"v\",\"actions\":["
                       "{\"op\":\"clearProjects\"}]}]}"),
        "clearProjects inside a variant drops it");
  check(previewDropped(
            "{\"reply\":\"p\",\"ask\":{\"question\":\"q?\",\"options\":["
            "{\"label\":\"a\",\"actions\":[{\"op\":\"clearProjects\"}]},{\"label\":\"b\"}]}}"),
        "clearProjects inside an ask-option preview drops the preview");
  // §10 removeProject's `current: true` form (exactly one of name/current).
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\",\"current\":true}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::REMOVE_PROJECT &&
              r.plan.actions[0].current && r.plan.actions[0].name.isEmpty(),
          "removeProject current:true parses");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                       "\"current\":false}]}").ok,
          "removeProject current:false fails (true is the only value)");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                       "\"name\":\"x\",\"current\":true}]}").ok,
          "removeProject with both name and current fails");
  }
  // §10 copy's `what` form.
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\",\"what\":\"layout\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::COPY && r.plan.actions[0].what == "layout",
          "copy what:layout parses");
    check(parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\","
                      "\"what\":\"image\"}]}").ok,
          "copy what:image parses");
    check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\","
                       "\"what\":\"lines\"}]}").ok,
          "copy what must be image|layout");
  }
  // §10 accent's `preset` form (exactly one of color/preset).
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\",\"preset\":\"green\"}]}");
    check(r.ok && r.plan.actions[0].preset == "green" && r.plan.actions[0].color.isEmpty(),
          "accent preset parses");
    check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\"}]}").ok,
          "accent with neither form fails");
    check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\","
                       "\"color\":\"#112233\",\"preset\":\"green\"}]}").ok,
          "accent with both forms fails");
    check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\","
                       "\"preset\":\"  \"}]}").ok,
          "blank accent preset fails");
  }
  // §10 lineStyle widening: pointColor ("" = follow stroke), drawMode, fillColor.
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\",\"pointColor\":\"\","
        "\"drawMode\":\"rect\",\"fillColor\":\"transparent\"}]}");
    check(r.ok && r.plan.actions[0].pointColorSet && r.plan.actions[0].pointColor.isEmpty(),
          "pointColor \"\" parses as an explicit follow-the-stroke");
    check(r.plan.actions[0].drawMode == "rect" &&
              r.plan.actions[0].fillColor == "transparent",
          "drawMode + fillColor kept");
    check(parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                      "\"pointColor\":\"#A1B2C3\"}]}").ok,
          "hex pointColor parses (and satisfies the at-least-one rule)");
    check(parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                      "\"fillColor\":\"#112233\"}]}").ok,
          "hex fillColor parses (skip-note is the executor's)");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                       "\"pointColor\":\"red\"}]}").ok,
          "pointColor rejects colour names (hex or \"\" only)");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                       "\"drawMode\":\"circle\"}]}").ok,
          "drawMode must be line|rect");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                       "\"fillColor\":\"none\"}]}").ok,
          "fillColor must be #rrggbb or transparent");
  }
  // §10 new rows: compare / zoom / renameProject / projectColor / blankColor /
  // openProject / incognito.
  std::printf("editor settings new rows (s10):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\",\"mode\":\"vertical\","
        "\"split\":0.25}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::COMPARE &&
              r.plan.actions[0].mode == "vertical" && r.plan.actions[0].split == 0.25,
          "compare with split parses");
    check(parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                      "\"mode\":\"none\"}]}").ok,
          "compare none parses");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"sideways\"}]}").ok,
          "unknown compare mode fails");
    // The divider belongs to the SPLIT modes only (registry onlyWith; fixture 160):
    // echoed beside "none"/"original" it fails the plan like any misplaced field.
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"none\",\"split\":0.5}]}").ok,
          "split with mode none fails (split modes only)");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"original\",\"split\":0.5}]}").ok,
          "split with mode original fails too");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"vertical\",\"split\":0.01}]}").ok,
          "split below 0.02 fails");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"horizontal\",\"split\":0.99}]}").ok,
          "split above 0.98 fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"percent\":150}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::ZOOM && r.plan.actions[0].percent == 150,
          "zoom percent parses");
    const auto f = parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"fit\":true}]}");
    check(f.ok && f.plan.actions[0].fit, "zoom fit:true parses");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\"}]}").ok,
          "zoom with neither form fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\","
                       "\"percent\":150,\"fit\":true}]}").ok,
          "zoom with both forms fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"percent\":4}]}").ok,
          "percent below 5 fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\","
                       "\"percent\":3201}]}").ok,
          "percent above 3200 fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"fit\":false}]}").ok,
          "fit:false fails (true is the only value)");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"r\",\"actions\":[{\"op\":\"renameProject\",\"name\":\" new name \"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::RENAME_PROJECT &&
              r.plan.actions[0].name == "new name",
          "renameProject parses (trimmed)");
    check(!parseOpPlan(QStringLiteral("{\"reply\":\"r\",\"actions\":[{\"op\":\"renameProject\","
                                      "\"name\":\"%1\"}]}")
                           .arg(QString(81, QLatin1Char('a')))).ok,
          "a renameProject name over 80 chars fails");
    check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"renameProject\"}]}").ok,
          "renameProject without a name fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"projectColor\",\"color\":\"#ec4899\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::PROJECT_COLOR &&
              r.plan.actions[0].color == "#ec4899",
          "projectColor hex parses");
    const auto clear = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"projectColor\",\"color\":\"\"}]}");
    check(clear.ok && clear.plan.actions[0].color.isEmpty(),
          "projectColor \"\" parses as the explicit clear");
    check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"projectColor\","
                       "\"color\":\"pink\"}]}").ok,
          "projectColor rejects colour names");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"b\",\"actions\":[{\"op\":\"blankColor\",\"color\":\"lightblue\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::BLANK_COLOR &&
              r.plan.actions[0].color == "lightblue",
          "blankColor CSS name parses");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blankColor\","
                       "\"color\":\"notacolor\"}]}").ok,
          "unknown blankColor name fails");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blankColor\"}]}").ok,
          "blankColor without a colour fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"o\",\"actions\":[{\"op\":\"openProject\",\"name\":\"portrait 1\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::OPEN_PROJECT &&
              r.plan.actions[0].name == "portrait 1",
          "openProject parses");
    check(!parseOpPlan(QStringLiteral("{\"reply\":\"o\",\"actions\":[{\"op\":\"openProject\","
                                      "\"name\":\"%1\"}]}")
                           .arg(QString(121, QLatin1Char('a')))).ok,
          "an openProject name over 120 chars fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\",\"on\":true}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::INCOGNITO && r.plan.actions[0].incognito,
          "incognito on:true parses");
    const auto off = parseOpPlan(
        "{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\",\"on\":false}]}");
    check(off.ok && !off.plan.actions[0].incognito, "incognito on:false parses");
    check(!parseOpPlan("{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\"}]}").ok,
          "incognito without on fails");
    check(!parseOpPlan("{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\","
                       "\"on\":\"yes\"}]}").ok,
          "non-boolean on fails");
  }
  {
    // Every new row is an editor-settings op: variant/ask-preview banned —
    // §1 drops the variant carrying one, keeping the plan alive.
    const char* banned[] = {
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"compare\",\"mode\":\"none\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"zoom\",\"fit\":true}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"renameProject\",\"name\":\"x\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"projectColor\",\"color\":\"\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"blankColor\",\"color\":\"red\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"openProject\",\"name\":\"x\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"incognito\",\"on\":true}]}]}",
    };
    bool allDropped = true;
    for (const char* json : banned) {
      const auto r = parseOpPlan(QString::fromUtf8(json));
      if (!r.ok || !r.plan.variants.isEmpty() || r.plan.warnings.size() != 1)
        allDropped = false;
    }
    check(allDropped, "every new editor row drops its variant with a warning");
    check(isEditorSettingsOp(OpKind::COMPARE) && isEditorSettingsOp(OpKind::INCOGNITO),
          "the new rows carry the editor-settings property");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"disconnect\","
                     "\"server\":\"  \"}]}")
             .ok,
        "blank disconnect server fails");
  {
    // Variants ban: a §10 op inside a variant costs THAT variant, not the plan.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"t\",\"actions\":["
        "{\"op\":\"theme\",\"mode\":\"dark\"}]}]}");
    check(r.ok && r.plan.variants.isEmpty(),
          "editor-settings op inside a variant drops the variant");
    check(r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant \"t\"") &&
              r.plan.warnings[0].contains("\"theme\" is an editor-settings op"),
          "…with a warning naming the variant and the op");
  }
  {
    // An unlabelled variant is named by its 1-based position instead.
    const auto r = parseOpPlan("{\"reply\":\"v\",\"variants\":[{\"actions\":["
                               "{\"op\":\"connect\",\"server\":\"a.example.com\"}]}]}");
    check(r.ok && r.plan.variants.isEmpty() && r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant 1"),
          "connect inside a variant drops it, named by position");
  }
  {
    // openUrl gets a steer, not just a drop: top-level, or the extension.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"actions\":["
        "{\"op\":\"openUrl\",\"url\":\"https://a.com/x.jpg\"}]}]}");
    check(r.ok && r.plan.variants.isEmpty() && r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("top-level action") &&
              r.plan.warnings[0].contains("extension assistant"),
          "openUrl inside a variant is dropped with the extension hint");
  }

  // ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──
  std::printf("multi-image ops (s2.1):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"both\",\"actions\":[{\"op\":\"image\",\"index\":2},"
        "{\"op\":\"save\",\"name\":\"portrait 1\"},{\"op\":\"save\"}]}");
    check(r.ok && r.plan.actions.size() == 3, "image + save parse");
    check(r.plan.actions[0].op == OpKind::IMAGE && r.plan.actions[0].index == 2,
          "image keeps its 1-based index");
    check(r.plan.actions[1].op == OpKind::SAVE && r.plan.actions[1].name == "portrait 1",
          "save keeps its name");
    check(r.plan.actions[2].op == OpKind::SAVE && r.plan.actions[2].name.isEmpty(),
          "a nameless save is allowed (the name is derived at execution)");
  }
  {
    // 1-based: 0, negatives and non-integers are not an attachment.
    const char* bad[] = {
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":0}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":-1}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":1.5}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":\"1\"}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\"}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":1,\"sneaky\":2}]}",
    };
    bool allRejected = true;
    for (const char* json : bad)
      if (parseOpPlan(QString::fromUtf8(json)).ok) allRejected = false;
    check(allRejected, "every bad image index rejects the plan");
  }
  {
    const QString longName = QString(121, QLatin1Char('x'));
    const auto r = parseOpPlan(
        QStringLiteral("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"name\":\"%1\"}]}")
            .arg(longName));
    check(!r.ok && r.error.contains("120"), "a save name over 120 chars is rejected");
    check(!parseOpPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"name\":5}]}").ok,
          "a non-string save name is rejected");
    check(!parseOpPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"as\":\"a\"}]}").ok,
          "an unknown save field is rejected");
    check(parseOpPlan(
              QStringLiteral("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"name\":\"%1\"}]}")
                  .arg(QString(120, QLatin1Char('x'))))
              .ok,
          "exactly 120 chars still fits");
  }
  {
    // Top-level only: neither may hide inside a variant or an ask-option preview
    // — §1 drops the variant/preview that carries one, never the plan.
    const auto v1 = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"a\",\"actions\":["
        "{\"op\":\"image\",\"index\":1}]}]}");
    check(v1.ok && v1.plan.variants.isEmpty() && v1.plan.warnings.size() == 1 &&
              v1.plan.warnings[0].contains("Dropped variant \"a\"") &&
              v1.plan.warnings[0].contains("top-level") &&
              v1.plan.warnings[0].contains("2.1"),
          "image inside a variant drops the variant");
    const auto v2 = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"a\",\"actions\":[{\"op\":\"save\"}]}]}");
    check(v2.ok && v2.plan.variants.isEmpty() &&
              v2.plan.warnings.value(0).contains("top-level"),
          "save inside a variant drops the variant");
    const auto a1 = parseOpPlan(
        "{\"reply\":\"x\",\"ask\":{\"question\":\"Q\",\"options\":["
        "{\"label\":\"A\",\"actions\":[{\"op\":\"save\"}]},{\"label\":\"B\"}]}}");
    check(a1.ok && a1.plan.ask.options.size() == 2 &&
              a1.plan.ask.options[0].actions.isEmpty() &&
              a1.plan.warnings.value(0).contains("top-level"),
          "save inside an ask-option preview drops the preview only");
    const auto a2 = parseOpPlan(
        "{\"reply\":\"x\",\"ask\":{\"question\":\"Q\",\"options\":["
        "{\"label\":\"A\",\"actions\":[{\"op\":\"image\",\"index\":1}]},{\"label\":\"B\"}]}}");
    check(a2.ok && a2.plan.ask.options.size() == 2 &&
              a2.plan.ask.options[0].actions.isEmpty(),
          "image inside an ask-option preview drops the preview only");
  }
  {
    // The two enum predicates stay apart: §2.1 ops are top-level only WITHOUT
    // being editor-settings ops (the §10 variant message is not theirs).
    check(isTopLevelOnlyOp(OpKind::IMAGE) && isTopLevelOnlyOp(OpKind::SAVE),
          "image/save are top-level only");
    check(!isEditorSettingsOp(OpKind::IMAGE) && !isEditorSettingsOp(OpKind::SAVE),
          "…but they are not editor-settings ops");
    check(isEditorSettingsOp(OpKind::THEME) && isEditorSettingsOp(OpKind::DISCONNECT) &&
              isTopLevelOnlyOp(OpKind::OPEN_URL),
          "the §10 ops keep both properties");
  }

  // ── variants + labels ──
  std::printf("variants:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[],\"variants\":["
        "{\"label\":\"rotated\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"right\"}]},"
        "{\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}]}");
    check(r.ok && r.plan.variants.size() == 2, "two variants parse");
    check(r.plan.variants[0].label == "rotated" &&
              r.plan.variants[0].actions.size() == 1,
          "variant label + actions kept");
    check(r.plan.variants[1].label.isEmpty(), "label-less variant allowed");
  }
  {
    // A KNOWN op with bad params inside a variant fails the whole plan.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"x\",\"actions\":["
        "{\"op\":\"rotate\",\"dir\":\"sideways\"}]}]}");
    check(!r.ok, "invalid known op in a variant fails the plan");
  }
  {
    // §1's one exception (the reported bug): one misplaced op used to destroy
    // the whole turn. The bad variant goes; the actions and the good variant run.
    const auto r = parseOpPlan(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}],\"variants\":["
        "{\"label\":\"wiped\",\"actions\":[{\"op\":\"clear\"}]},"
        "{\"label\":\"sepia\",\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}]}");
    check(r.ok && r.error.isEmpty(), "a misplaced settings op no longer fails the plan");
    check(r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::ROTATE,
          "the top-level actions survive");
    check(r.plan.variants.size() == 1 && r.plan.variants[0].label == "sepia",
          "the well-formed variant survives");
    check(r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant \"wiped\"") &&
              r.plan.warnings[0].contains("\"clear\" is an editor-settings op") &&
              r.plan.warnings[0].contains("top-level actions"),
          "the warning names the dropped variant, the op, and where it belongs");
  }
  {
    // A plan made ONLY of such a variant: reply + warning, nothing to execute.
    const auto r = parseOpPlan(
        "{\"reply\":\"here you go\",\"variants\":[{\"label\":\"v\",\"actions\":["
        "{\"op\":\"clear\"}]}]}");
    check(r.ok && r.plan.reply == "here you go", "…and the reply still stands");
    check(r.plan.actions.isEmpty() && r.plan.variants.isEmpty() &&
              r.plan.warnings.size() == 1,
          "nothing executes, one warning explains why");
  }
  {
    // A dropped variant takes its OWN warnings with it — a render that never
    // happens has nothing to report.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"v\",\"actions\":["
        "{\"op\":\"wobble\"},{\"op\":\"save\"}]}]}");
    check(r.ok && r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant"),
          "the dropped variant's unknown-op warning goes with it");
  }
  check(sanitizeLabel("  Sepia / warm! <v1>  ") == "Sepia warm v1", "labels sanitized");
  check(sanitizeLabel("###") .isEmpty(), "all-symbol label sanitizes to empty");


  // ── §11 interactive replies (`ask`) ──
  std::printf("ask (contract 11):\n");
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}})");
    check(r.ok, "a card parses");
    check(r.plan.ask.question == "Which tint?", "question kept");
    check(!r.plan.ask.multi, "single is the default mode");
    check(!r.plan.ask.allowCustom, "allowCustom defaults off");
    check(r.plan.ask.options.size() == 2 && r.plan.ask.options[0].label == "Sepia", "options kept in order");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"  Which?  ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":"  A  "},{"label":"B"}]}})");
    check(r.ok && r.plan.ask.multi, "multi mode");
    check(r.plan.ask.allowCustom && r.plan.ask.customLabel == "Other", "custom row, trimmed");
    check(r.plan.ask.question == "Which?" && r.plan.ask.options[0].label == "A", "strings trimmed");
  }
  {
    const auto r = parseOpPlan(R"({"version":1,"reply":"hi","actions":[]})");
    check(r.ok && r.plan.ask.options.isEmpty(), "no card on an ordinary turn");
    const auto c = parseOpPlan("just chatting");
    check(c.ok && c.plan.ask.options.isEmpty(), "no card on a chat-only turn");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Left","actions":[{"op":"rotate","dir":"left"}]}]}})");
    check(r.ok && r.plan.ask.options[0].actions.size() == 1, "preview actions parsed");
    check(r.plan.ask.options[1].actions.size() == 1, "each option keeps its own preview");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Web","image":{"url":"https://example.com/cat.jpg"}},{"label":"Saved","image":{"projectId":"p_12"}}]}})");
    check(r.ok && r.plan.ask.options[0].imageUrl == "https://example.com/cat.jpg", "http image reference kept");
    check(r.plan.ask.options[1].projectId == "p_12", "project reference kept");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Page","image":{"scanIndex":3}},{"label":"Plain"}]}})");
    check(r.ok && r.plan.ask.options.size() == 2, "a scan reference keeps the option");
    check(r.plan.ask.options[0].imageUrl.isEmpty() && !r.plan.warnings.isEmpty(), "...but loses the picture, with a warning");
  }
  {
    const char* bad[] = {
        R"({"version":1,"reply":"x","ask":"hello"})",
        R"({"version":1,"reply":"x","ask":{"options":[{"label":"A"},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"  ","options":[{"label":"A"},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"only"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":""},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"https://e/x","projectId":"p"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"data:image/png;base64,AA"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"file:///etc/passwd"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}})",
    };
    bool allRejected = true;
    for (const char* json : bad) {
      if (parseOpPlan(QString::fromUtf8(json)).ok) allRejected = false;
    }
    check(allRejected, "every malformed card rejects the whole plan");
  }
  {
    // §1/§11.2: the misplaced op costs the PREVIEW; the option is still offered.
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"Dark","actions":[{"op":"theme","mode":"dark"}]},{"label":"B"}]}})");
    check(r.ok && r.plan.ask.options.size() == 2 &&
              r.plan.ask.options[0].label == "Dark" &&
              r.plan.ask.options[0].actions.isEmpty(),
          "an editor-settings op inside a preview drops the preview, not the plan");
    check(r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("preview for option 1 \"Dark\"") &&
              r.plan.warnings[0].contains("still offered"),
          "…with a warning naming the option");
  }
  {
    check(askAnswerText({"Sepia"}) == "Sepia", "one pick becomes the answer");
    check(askAnswerText({"Sepia", "B&W"}) == "Sepia, B&W", "several picks join");
    check(askAnswerText({}).isEmpty(), "no pick, no answer");
    check(askAnswerText({"Sepia"}, "  a warm green  ") == "a warm green", "typed text wins, trimmed");
    check(askAnswerText({"A", "   ", ""}) == "A", "blank labels never pad the answer");
    const int answerCap = OpSchema::desktop().limit("ask.answer");
    check(answerCap == 500 && askAnswerText({}, QString(answerCap + 20, 'x')).size() == answerCap,
          "answer capped at the registry's ask.answer limit");
  }

  // ── planTouchesTheImage: what makes an attachment worth adopting ──
  // An EDITING plan arriving with an empty canvas takes the attached picture as the
  // working image; a question about it, or a settings change, leaves the canvas alone.
  {
    using stencil::llm::planTouchesTheImage;
    const auto planOf = [](const char* json) {
      return stencil::llm::parseOpPlan(QString::fromUtf8(json)).plan;
    };
    check(planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"filter","mode":"bw"}],"variants":[]})")),
          "a filter edits the image");
    check(planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"crop","spec":{"x1":"10%"}}],"variants":[]})")),
          "so does a crop");
    check(planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[],"variants":[{"label":"a","actions":[{"op":"filter","mode":"bw"}]}]})")),
          "variants are alternatives OF the image, so they count");
    check(!planTouchesTheImage(planOf(R"({"version":1,"reply":"just answering","actions":[],"variants":[]})")),
          "a chat-only turn does not");
    check(!planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"theme","mode":"dark"}],"variants":[]})")),
          "and neither does a settings op");
    // §2.1: switching to an attachment (or saving) is not itself an edit — the
    // ops around it are what make the plan worth adopting a picture for.
    check(!planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"image","index":1},{"op":"save"}],"variants":[]})")),
          "image/save alone do not count as editing the picture");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

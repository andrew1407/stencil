// Creating a blank, the §10 editor setters, and the runtime error paths.
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkEditorSettings(const QImage& img, const stencil::core::PageSize& a4) {
  // ── blank ──
  std::printf("blank:\n");
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"b","actions":[{"op":"blank","color":"red","format":"a10"}]})");
    check(parsed.ok, "blank plan parses");
    CanvasPlanTarget target(QImage(), a4);
    check(!target.hasImage(), "target starts imageless");
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "blank creates without a prior image");
    const QImage result = target.renderResult();
    // a10 = 2.6 x 3.7 cm at 96 dpi -> 98 x 140 px (core defaultBlankSizePx).
    check(result.width() == 98 && result.height() == 140, "a10 blank is 98x140 @ 96dpi");
    const QColor c = result.pixelColor(50, 70);
    check(c.red() == 255 && c.green() == 0 && c.blue() == 0, "blank filled with CSS red");
  }

  // ── §10 editor-settings ops ──
  std::printf("editor settings (s10):\n");
  {
    // Recording target: CanvasPlanTarget records the settings calls; connect /
    // disconnect resolve against a stub saved/live store via resolveServerRef.
    struct SettingsTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList saved{QStringLiteral("https://a.example.com:8090"),
                        QStringLiteral("https://b.example.com")};
      QStringList connected, disconnected;
      bool connectServer(const QString& ref, QString* err) override {
        const QString url = resolveServerRef(ref, saved);
        if (url.isEmpty()) {
          if (err) *err = QStringLiteral("connect: unknown server \"%1\"").arg(ref);
          return false;
        }
        connected << url;
        return true;
      }
      bool disconnectServer(const QString& ref, QString* err) override {
        const QString url = resolveServerRef(ref, connected);
        if (url.isEmpty()) {
          if (err) *err = QStringLiteral("disconnect: unknown server \"%1\"").arg(ref);
          return false;
        }
        disconnected << url;
        return true;
      }
    };

    const auto parsed = parseOpPlan(R"({
      "reply": "settings",
      "actions": [
        {"op": "theme", "mode": "dark"},
        {"op": "accent", "color": "#7c3aed"},
        {"op": "lineStyle", "color": "#00ff00", "thickness": 3, "style": "dashed"},
        {"op": "units", "value": "in"},
        {"op": "view", "points": false},
        {"op": "connect", "server": "b.example.com"},
        {"op": "disconnect", "server": "https://b.example.com"}
      ]
    })");
    check(parsed.ok, "settings plan parses");
    SettingsTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "settings plan executes");
    check(target.themeMode == "dark" && target.accentColor == "#7c3aed",
          "theme + accent recorded");
    check(target.lsColor == "#00ff00" && target.lsThickness == 3 &&
              target.lsStyle == "dashed" && target.lsPointSize == 0,
          "lineStyle subset recorded (absent field untouched)");
    check(target.unitsValue == "in", "units recorded");
    check(target.viewPoints == 0 && target.viewLines == -1,
          "view points=false recorded, lines untouched");
    check(target.connected == QStringList{"https://b.example.com"},
          "connect resolved the saved host to its stored URL");
    check(target.disconnected == QStringList{"https://b.example.com"},
          "disconnect resolved the live URL");
    check(target.renderResult().size() == img.size(),
          "settings ops never touch the working image");

    // Unsaved server → the typed unknown-server plan error, nothing connects.
    const auto evil = parseOpPlan(
        R"({"reply":"c","actions":[{"op":"connect","server":"evil.example.com"}]})");
    SettingsTarget t2(img, a4);
    const ExecResult res2 = executePlan(evil.plan, t2);
    check(!res2.ok && res2.error.contains("unknown server"),
          "connect to an unsaved server fails the plan");
    check(t2.connected.isEmpty(), "nothing connected on the failed plan");
  }
  {
    // resolveServerRef: exact URL, unique host, ambiguous host, unknown.
    const QStringList saved{QStringLiteral("https://a.example.com:8090"),
                            QStringLiteral("https://a.example.com:9000"),
                            QStringLiteral("https://b.example.com")};
    check(resolveServerRef("https://a.example.com:9000", saved) ==
              "https://a.example.com:9000",
          "exact URL match wins");
    check(resolveServerRef("b.example.com", saved) == "https://b.example.com",
          "unique host resolves to the stored URL");
    check(resolveServerRef("a.example.com", saved).isEmpty(),
          "ambiguous host refuses to resolve");
    check(resolveServerRef("c.example.com", saved).isEmpty(), "unknown host is empty");
  }

  // ── runtime errors ──
  std::printf("errors:\n");
  {
    const auto parsed =
        parseOpPlan(R"({"reply":"v","actions":[{"op":"frame","index":0}]})");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(!res.ok && res.error.contains("not a video"),
          "frame off-video is a plan-level error");
  }
  {
    const auto parsed =
        parseOpPlan(R"({"reply":"r","actions":[{"op":"rotate","dir":"left"}]})");
    CanvasPlanTarget target(QImage(), a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(!res.ok && res.error.contains("no working image"),
          "image ops require a working image");
  }
  {
    // frame inside a variant is rejected even on a video-ish target.
    const auto parsed = parseOpPlan(
        R"({"reply":"v","variants":[{"label":"f","actions":[{"op":"frame","index":0}]}]})");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(!res.ok && res.error.contains("variant"), "frame inside a variant rejected");
  }

  }

}  // namespace llmexec

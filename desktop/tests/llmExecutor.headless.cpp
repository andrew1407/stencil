// Headless check of the LLM plan executor (src/llm/planExecutor) against the
// real CanvasWidget, seeded with the committed PNG fixture (16x12 solid
// #3366cc) — the llm-contract.md §1-2 execution semantics: top-level
// actions mutate the working image in order; each variant branches from the
// state AFTER those actions and yields one separate image; `frame` is a
// plan-level error off-video; layout coordinates are model-frame and get
// re-mapped through earlier crop/rotate + clamped (§1 coordinate re-mapping).
// Runs offscreen (QT_QPA_PLATFORM=offscreen).
#include "imageOps.hpp"
#include "opPlan.hpp"
#include "planExecutor.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace stencil::llm;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  const QString fixture = QStringLiteral(STENCIL_FIXTURES_DIR "/sample.png");
  QImage img;
  check(img.load(fixture), "fixture PNG loaded");
  check(img.width() == 16 && img.height() == 12, "fixture is 16x12");

  const stencil::core::PageSize a4{21.0, 29.7};

  // ── actions + 2 variants ──
  std::printf("actions + variants:\n");
  {
    const auto parsed = parseOpPlan(R"({
      "version": 1,
      "reply": "cropped, rotated, desaturated; two variants",
      "actions": [
        {"op": "crop", "spec": {"x1": "2px", "y1": "2", "x2": "14px", "y2": "10px"}},
        {"op": "rotate", "dir": "right"},
        {"op": "filter", "mode": "bw"},
        {"op": "formula", "axis": "x", "expr": "x*2"},
        {"op": "page", "format": "a5"}
      ],
      "variants": [
        {"label": "Sepia one", "actions": [{"op": "filter", "mode": "sepia"}]},
        {"label": "tinted!", "actions": [
          {"op": "filter", "mode": "custom", "tint": "#ff0000"},
          {"op": "rotate", "dir": "left"}
        ]}
      ]
    })");
    check(parsed.ok, "plan parses");

    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "plan executes");
    check(res.changed, "top-level actions reported as changes");

    // crop 2..14 x 2..10 -> 12x8, then one clockwise quarter -> 8x12.
    const QImage result = target.renderResult();
    check(result.width() == 8 && result.height() == 12,
          "crop (12x8) + rotate right -> 8x12 result");
    {
      const QColor c = result.pixelColor(4, 6);
      check(c.red() == c.green() && c.green() == c.blue(),
            "bw filter left a grayscale pixel");
      check(c.red() > 0 && c.red() < 255, "gray value is a real mix (not black/white)");
    }
    check(target.formulaX == "x*2" && target.formulaY.isEmpty(),
          "formula op recorded on its axis");
    check(target.pageFormat == "A5", "page op adopted (uppercased canonical name)");

    // Variants: branch from the post-actions state (8x12 bw), one image each.
    check(res.variants.size() == 2, "two variant images produced");
    if (res.variants.size() == 2) {
      check(res.variants[0].first == "Sepia one", "variant label kept");
      check(res.variants[1].first == "tinted", "variant label sanitized");
      const QImage& sepia = res.variants[0].second;
      check(sepia.width() == 8 && sepia.height() == 12,
            "sepia variant keeps the post-actions size");
      const QColor sc = sepia.pixelColor(4, 6);
      check(sc.red() > sc.blue(), "sepia variant pixel is warm (r > b)");
      const QImage& tinted = res.variants[1].second;
      check(tinted.width() == 12 && tinted.height() == 8,
            "tinted variant applied its own rotate (12x8)");
      const QColor tc = tinted.pixelColor(6, 4);
      check(tc.red() > tc.blue() && tc.red() > tc.green(),
            "custom #ff0000 tint reddens the variant");
    }

    // The variants never touched the working target.
    check(target.renderResult().width() == 8, "working image untouched by variants");
  }

  // ── §1 leniency: a variant carrying a settings op is dropped, not fatal ──
  std::printf("dropped variant (contract 1):\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "rotated, plus one variant",
      "actions": [{"op": "rotate", "dir": "right"}],
      "variants": [
        {"label": "wiped", "actions": [{"op": "clear"}]},
        {"label": "sepia", "actions": [{"op": "filter", "mode": "sepia"}]}
      ]
    })");
    check(parsed.ok && parsed.error.isEmpty(), "the plan parses instead of failing");
    check(parsed.plan.warnings.size() == 1 &&
              parsed.plan.warnings[0].contains("Dropped variant \"wiped\""),
          "the dropped variant is a warning, not an error");

    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.changed, "the top-level actions still execute");
    check(target.renderResult().width() == 12 && target.renderResult().height() == 16,
          "the rotate ran (16x12 -> 12x16)");
    check(!target.cleared, "the misplaced clear never reached the editor");
    check(res.variants.size() == 1 && res.variants[0].first == "sepia",
          "only the well-formed variant renders");
  }

  // ── layout draws onto the render ──
  std::printf("layout:\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "line drawn",
      "actions": [{"op": "layout", "lines": [
        {"points": [{"x": 0, "y": 6}, {"x": 15, "y": 6}],
         "color": "#FF0000", "thickness": 3}
      ]}]
    })");
    check(parsed.ok, "layout plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "layout plan executes");
    const QImage result = target.renderResult();
    check(result.size() == img.size(), "layout leaves the pixels' size alone");
    const QColor c = result.pixelColor(8, 6);
    check(c.red() > 200 && c.green() < 100, "the drawn line shows in the render");
  }

  // ── §1 coordinate re-mapping: plan coords are model-frame ──
  std::printf("coordinate re-mapping (s1):\n");
  // Captures the lines the executor hands to the canvas (post-map, post-clamp).
  struct LayoutRecorder : CanvasPlanTarget {
    using CanvasPlanTarget::CanvasPlanTarget;
    stencil::core::Lines got;
    void setLayoutLines(const stencil::core::Lines& lines) override {
      got = lines;
      CanvasPlanTarget::setLayoutLines(lines);
    }
  };
  const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
  {
    // crop-then-layout: the crop origin (2,2) is subtracted from later points;
    // a point that lands left of the crop clamps to the new frame's edge.
    const auto parsed = parseOpPlan(R"({
      "reply": "crop then draw",
      "actions": [
        {"op": "crop", "spec": {"x1": "2px", "y1": "2", "x2": "14px", "y2": "10px"}},
        {"op": "layout", "lines": [
          {"points": [{"x": 2, "y": 6}, {"x": 13.5, "y": 6.25}, {"x": 0, "y": 0}],
           "color": "#FF0000", "thickness": 3}
        ]}
      ]
    })");
    check(parsed.ok, "crop+layout plan parses");
    LayoutRecorder target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "crop+layout plan executes");
    check(target.got.size() == 1 && target.got[0].points.size() == 3,
          "layout line reached the canvas");
    if (target.got.size() == 1 && target.got[0].points.size() == 3) {
      const auto& p = target.got[0].points;
      check(near(p[0].x, 0) && near(p[0].y, 4), "point translated by -crop origin");
      check(near(p[1].x, 11.5) && near(p[1].y, 4.25), "fractional point translated too");
      check(near(p[2].x, 0) && near(p[2].y, 0), "point left of the crop clamps to 0,0");
    }
    const QImage result = target.renderResult();
    check(result.width() == 12 && result.height() == 8, "cropped frame is 12x8");
    const QColor c = result.pixelColor(6, 4);
    check(c.red() > 200 && c.green() < 100, "re-mapped line draws inside the crop");
  }
  {
    // Crop with the §2 aspect key: the executor forwards it into the core
    // CropSpec, and resolveCropRect shrinks the wider dimension symmetrically
    // about the centre — 16x12 at "1:1" lands on the centred 12x12 square.
    const auto parsed = parseOpPlan(R"({
      "reply": "square",
      "actions": [{"op": "crop", "spec": {"aspect": "1:1"}}]
    })");
    check(parsed.ok, "aspect-only crop plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.changed, "aspect-only crop executes");
    const QImage result = target.renderResult();
    check(result.width() == 12 && result.height() == 12,
          "aspect crop reached core resolveCropRect (16x12 -> centred 12x12)");
  }
  {
    // rotate-then-layout, direction validated against core's rotate: mark the
    // pixel (3,2), rotate right; core rotateImageRGBA (quarters=1, clockwise)
    // moves the mark to (h-1-y, x) = (9,3) — the app's rotate and the
    // executor's point mapping must land the same place.
    std::vector<std::uint8_t> src(16 * 12 * 4, 0), dst(16 * 12 * 4, 0);
    src[(2 * 16 + 3) * 4] = 255;  // mark (3,2)
    stencil::core::rotateImageRGBA(src.data(), 16, 12, 1, dst.data());
    check(dst[(3 * 12 + 9) * 4] == 255, "core rotate cw puts (3,2) at (9,3)");

    // The app's rotate agrees: rotate-only plan on a marked image.
    QImage marked(16, 12, QImage::Format_RGB32);
    marked.fill(QColor(0x33, 0x66, 0xcc));
    marked.setPixelColor(3, 2, QColor(Qt::red));
    {
      const auto rotOnly =
          parseOpPlan(R"({"reply":"r","actions":[{"op":"rotate","dir":"right"}]})");
      CanvasPlanTarget target(marked, a4);
      check(executePlan(rotOnly.plan, target).ok, "rotate-only plan executes");
      const QImage result = target.renderResult();
      check(result.width() == 12 && result.height() == 16, "rotated frame is 12x16");
      const QColor mc = result.pixelColor(9, 3);
      check(mc.red() > 200 && mc.blue() < 100, "app rotate matches core: mark at (9,3)");
    }
    // And the executor's point mapping follows the same turn.
    const auto parsed = parseOpPlan(R"({
      "reply": "rotate then draw",
      "actions": [
        {"op": "rotate", "dir": "right"},
        {"op": "layout", "lines": [
          {"points": [{"x": 3.5, "y": 2.5}, {"x": 14, "y": 6}],
           "color": "#00FF00", "thickness": 1}
        ]}
      ]
    })");
    check(parsed.ok, "rotate+layout plan parses");
    LayoutRecorder target(marked, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "rotate+layout plan executes");
    check(target.got.size() == 1 && target.got[0].points.size() == 2,
          "rotated layout line reached the canvas");
    if (target.got.size() == 1 && target.got[0].points.size() == 2) {
      const auto& p = target.got[0].points;
      // cw mapping (x,y) -> (h-y, x) with h=12: the marked pixel's center
      // (3.5,2.5) lands in the same pixel core moved the mark to; (14,6)->(6,14).
      check(near(p[0].x, 9.5) && near(p[0].y, 3.5),
            "point followed the marked pixel through the rotate");
      check(near(p[1].x, 6) && near(p[1].y, 14), "second point quarter-turned");
    }
  }
  {
    // clamp on a plan with NO crop/rotate: out-of-bounds points pull into the
    // working image's bounds before drawing.
    const auto parsed = parseOpPlan(R"({
      "reply": "clamped",
      "actions": [{"op": "layout", "lines": [
        {"points": [{"x": -5, "y": 6}, {"x": 100, "y": 20}],
         "color": "#FF0000", "thickness": 3}
      ]}]
    })");
    check(parsed.ok, "clamp plan parses");
    LayoutRecorder target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "clamp plan executes");
    check(target.got.size() == 1 && target.got[0].points.size() == 2,
          "clamped line reached the canvas");
    if (target.got.size() == 1 && target.got[0].points.size() == 2) {
      const auto& p = target.got[0].points;
      check(near(p[0].x, 0) && near(p[0].y, 6), "negative x clamps to 0");
      check(near(p[1].x, 16) && near(p[1].y, 12), "overshoot clamps to the 16x12 bounds");
    }
  }
  {
    // A variant branches from the post-actions state — its model-frame coords
    // inherit the top-level crop's translation.
    const auto parsed = parseOpPlan(R"({
      "reply": "variant inherits the map",
      "actions": [
        {"op": "crop", "spec": {"x1": "2px", "y1": "2", "x2": "14px", "y2": "10px"}}
      ],
      "variants": [
        {"label": "drawn", "actions": [{"op": "layout", "lines": [
          {"points": [{"x": 2, "y": 6}, {"x": 13, "y": 6}],
           "color": "#FF0000", "thickness": 3}
        ]}]}
      ]
    })");
    check(parsed.ok, "variant re-map plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "variant re-map plan executes");
    check(res.variants.size() == 1, "one variant image produced");
    if (res.variants.size() == 1) {
      const QImage& v = res.variants[0].second;
      check(v.width() == 12 && v.height() == 8, "variant keeps the cropped frame");
      // (9,4) is on the stroke away from the point/midpoint markers.
      const QColor on = v.pixelColor(9, 4);
      check(on.red() > 200 && on.green() < 100, "variant line translated to y=4");
      const QColor off = v.pixelColor(6, 7);
      check(off.red() < 150, "no line at the un-translated y");
    }
  }

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

  // ── §10 openUrl: the user-echo guard + the injected loader ──
  std::printf("openUrl:\n");
  {
    struct UrlTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QString typed;
      QStringList opened;
      QString userTypedText() const override { return typed; }
      bool openUrl(const QString& url, bool incognito, QString*) override {
        opened << (incognito ? url + "#incognito" : url);
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"o","actions":[{"op":"openUrl","url":"https://a.com/cat.jpg","incognito":true}]})");
    check(parsed.ok, "openUrl plan parses");
    {
      // The user never typed the URL → blocked, the target is never reached.
      UrlTarget target(img, a4);
      target.typed = "open something nice";
      const ExecResult res = executePlan(parsed.plan, target);
      check(!res.ok && res.error.contains("not a URL you gave"), "un-echoed URL blocked");
      check(target.opened.isEmpty(), "blocked openUrl never reaches the loader");
    }
    {
      // Echoed from the user's own message → the loader runs with the flag.
      UrlTarget target(img, a4);
      target.typed = "please open https://a.com/cat.jpg in incognito";
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "echoed openUrl executes");
      check(target.opened == QStringList{"https://a.com/cat.jpg#incognito"},
            "loader got the URL + incognito");
    }
    {
      // The base PlanTarget has no loader — a typed failure, not a crash.
      CanvasPlanTarget target(img, a4);
      const auto p2 = parseOpPlan(
          R"({"reply":"o","actions":[{"op":"openUrl","url":"https://a.com/cat.jpg"}]})");
      const ExecResult res = executePlan(p2.plan, target);
      check(!res.ok, "default target rejects openUrl (guard or loader)");
    }
  }

  // ── §10 openFile: the same echo guard, pointed at the filesystem ──
  std::printf("openFile:\n");
  {
    struct FileTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QString typed;
      QStringList opened;
      QString userTypedText() const override { return typed; }
      bool openFile(const QString& path, QString*) override {
        opened << path;
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"o","actions":[{"op":"openFile","path":"~/Pictures/cat.png"}]})");
    check(parsed.ok, "openFile plan parses");
    {
      // A path the user never wrote is blocked before any read happens.
      FileTarget target(img, a4);
      target.typed = "open something nice";
      const ExecResult res = executePlan(parsed.plan, target);
      check(!res.ok && res.error.contains("not a path you gave"), "un-echoed path blocked");
      check(target.opened.isEmpty(), "blocked openFile never reaches the filesystem");
    }
    {
      // Echoed by the user → the read runs, with the path exactly as written.
      FileTarget target(img, a4);
      target.typed = "please open ~/Pictures/cat.png";
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "echoed openFile executes");
      check(target.opened == QStringList{"~/Pictures/cat.png"}, "target got the path as typed");
    }
    {
      // The base PlanTarget cannot read files — a typed failure, not a crash.
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(!res.ok, "default target rejects openFile (guard or reader)");
    }
    // Only the formats the editor opens, and never a URL or a folder.
    check(!parseOpPlan(R"({"reply":"o","actions":[{"op":"openFile","path":"~/notes.txt"}]})").ok,
          "an unopenable file type is rejected at parse");
    check(!parseOpPlan(R"({"reply":"o","actions":[{"op":"openFile","path":"~/Pictures"}]})").ok,
          "a folder is rejected at parse");
    check(!parseOpPlan(
               R"({"reply":"o","actions":[{"op":"openFile","path":"https://a.com/cat.png"}]})")
               .ok,
          "a URL is not a local path");
  }

  // ── §2.1 save: the §10 destination rides the same echo rule ──
  std::printf("save destination:\n");
  {
    struct SaveTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QString typed;
      QStringList dests;
      QString userTypedText() const override { return typed; }
      bool saveProject(const QString&, const QString& dest, QString*) override {
        dests << dest;
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"s","actions":[{"op":"save","name":"a","path":"~/Downloads"}]})");
    check(parsed.ok, "save with a destination parses");
    {
      SaveTarget target(img, a4);
      target.typed = "save it into ~/Downloads";
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.dests == QStringList{"~/Downloads"}, "echoed destination is used");
    }
    {
      // Not echoed: the save still happens, into the editor's own store, with a note.
      SaveTarget target(img, a4);
      target.typed = "save it";
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.dests == QStringList{""}, "un-echoed destination is dropped");
      check(res.notes.join(" ").contains("not a path you gave"), "and the drop is noted");
    }
    check(!parseOpPlan(
               R"({"reply":"s","actions":[{"op":"save","path":"https://x.example/o.png"}]})")
               .ok,
          "a URL is not a save destination");
    {
      // Naming the FOLDER is how people grant a destination — the file inside it is the
      // model's to name (this is what left Downloads empty before).
      const auto named = parseOpPlan(
          R"({"reply":"s","actions":[{"op":"save","path":"/Users/me/Downloads/portrait-bw.png"}]})");
      SaveTarget target(img, a4);
      target.typed = "retry the save to /Users/me/Downloads with explicit file names";
      const ExecResult res = executePlan(named.plan, target);
      check(res.ok && target.dests == QStringList{"/Users/me/Downloads/portrait-bw.png"},
            "a file inside a named folder is allowed");
    }
    {
      // …but a sibling folder was never granted, and `..` voids the grant.
      SaveTarget target(img, a4);
      target.typed = "save to /Users/me/Downloads";
      const auto out = parseOpPlan(
          R"({"reply":"s","actions":[{"op":"save","path":"/Users/me/Documents/x.png"}]})");
      check(executePlan(out.plan, target).ok && target.dests == QStringList{""},
            "a sibling folder is not granted");
      const auto up = parseOpPlan(
          R"({"reply":"s","actions":[{"op":"save","path":"/Users/me/Downloads/../.ssh/k.png"}]})");
      SaveTarget climb(img, a4);
      climb.typed = "save to /Users/me/Downloads";
      check(executePlan(up.plan, climb).ok && climb.dests == QStringList{""},
            "a .. climb out of the named folder is not granted");
    }
  }

  // ── §10 copy: the clipboard hand-off + the no-image note ──
  std::printf("copy:\n");
  {
    const auto parsed = parseOpPlan(R"({"reply":"c","actions":[{"op":"copy"}]})");
    check(parsed.ok, "copy plan parses");
    {
      // With a working image the target's clipboard path runs.
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.copied && res.notes.isEmpty(),
            "copy reaches the target's clipboard path");
    }
    {
      // Copying nothing is a skipped action + note, never a failed plan (§10).
      CanvasPlanTarget target(QImage(), a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && !target.copied, "copy with no image does not fail the plan");
      check(res.notes.size() == 1 && res.notes.first().contains("no working image"),
            "…it lands as a skipped-copy note");
    }
    {
      // The base PlanTarget has no clipboard — a typed failure, not a crash.
      struct BareTarget : CanvasPlanTarget {
        using CanvasPlanTarget::CanvasPlanTarget;
        bool copyImage(QString* err) override { return PlanTarget::copyImage(err); }
      };
      BareTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(!res.ok && res.error.contains("copy"), "clipboard-less target rejects copy");
    }
  }

  // ── §10 project management: notes surface, defaults reject ──
  std::printf("project management:\n");
  {
    // The injected flows run in plan order; a non-empty note (unknown name,
    // declined confirm, empty store) surfaces without failing the plan.
    struct ProjectsTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList calls;
      QString removeNote, clearNote;   // "" = success (nothing to report)
      bool removeProjectNamed(const QString& name, bool current, QString* note) override {
        calls << (current ? QStringLiteral("remove:<current>")
                          : QStringLiteral("remove:%1").arg(name));
        *note = removeNote;
        return true;
      }
      bool clearProjects(bool keepCurrent, QString* note) override {
        calls << (keepCurrent ? QStringLiteral("clear:<others>") : QStringLiteral("clear"));
        *note = clearNote;
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"p","actions":[{"op":"removeProject","name":"a"},{"op":"clearProjects"}]})");
    check(parsed.ok, "project-management plan parses");
    {
      ProjectsTarget target(img, a4);
      target.removeNote = QStringLiteral("removal canceled");
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.calls == QStringList({"remove:a", "clear"}),
            "both ops reach the target's flows, in order");
      check(res.notes.size() == 1 &&
                res.notes.first() == "removeProject: removal canceled",
            "a declined remove lands as a note, never a failed plan");
    }
    {
      ProjectsTarget target(img, a4);
      target.clearNote = QStringLiteral("no saved projects to clear");
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes.size() == 1 &&
                res.notes.first() == "clearProjects: no saved projects to clear",
            "an empty store lands as a clearProjects note");
    }
    {
      // The base PlanTarget cannot manage projects — a typed failure each.
      CanvasPlanTarget target(img, a4);
      const ExecResult r1 = executePlan(
          parseOpPlan(R"({"reply":"p","actions":[{"op":"removeProject","name":"a"}]})").plan,
          target);
      check(!r1.ok && r1.error.contains("removeProject") &&
                r1.error.contains("not available here"),
            "default target rejects removeProject");
      const ExecResult r2 = executePlan(
          parseOpPlan(R"({"reply":"p","actions":[{"op":"clearProjects"}]})").plan, target);
      check(!r2.ok && r2.error.contains("clearProjects") &&
                r2.error.contains("not available here"),
            "default target rejects clearProjects");
    }
  }

  // ── §10 clearChat: deferred to the plan's end, three-valued, scope-banned ──
  std::printf("clearChat (s10):\n");
  {
    // Records the order the flows run in: clearChat is listed FIRST but must
    // reach its hook LAST (contract §10 end-of-turn deferral).
    struct ChatTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList calls;
      QString chatNote;   // "" = cleared (nothing to report)
      void setUnits(const QString& v) override {
        CanvasPlanTarget::setUnits(v);
        calls << QStringLiteral("units");
      }
      bool clearChat(QString* note) override {
        calls << QStringLiteral("clearChat");
        *note = chatNote;
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"c","actions":[{"op":"clearChat"},{"op":"units","value":"in"}]})");
    check(parsed.ok, "clearChat plan parses");
    {
      ChatTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.changed, "clearChat plan executes");
      check(target.calls == QStringList({"units", "clearChat"}),
            "clearChat runs LAST even when listed first");
      check(res.notes.isEmpty(), "a clean clear reports nothing");
    }
    {
      // Declined confirm: a note, never a failed plan (the other action ran).
      ChatTarget target(img, a4);
      target.chatNote = QStringLiteral("clear canceled");
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes == QStringList({"clearChat: clear canceled"}),
            "a declined confirm lands as a clearChat note");
      check(target.unitsValue == "in", "the plan's other action still ran");
    }
    {
      // The plain sandbox target records the request; the base target rejects.
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(
          parseOpPlan(R"({"reply":"c","actions":[{"op":"clearChat"}]})").plan, target);
      check(res.ok && target.chatCleared, "CanvasPlanTarget records the deferred clear");
      struct BareTarget : CanvasPlanTarget {
        using CanvasPlanTarget::CanvasPlanTarget;
        bool clearChat(QString* note) override { return PlanTarget::clearChat(note); }
      };
      BareTarget bare(img, a4);
      const ExecResult r2 = executePlan(
          parseOpPlan(R"({"reply":"c","actions":[{"op":"clearChat"}]})").plan, bare);
      check(!r2.ok && r2.error.contains("clearChat") &&
                r2.error.contains("no conversation"),
            "chat-less base target rejects clearChat");
    }
    // Strict fields + the variant / ask-preview scope (§1: the variant/preview
    // is dropped with a warning, the plan lives).
    check(!parseOpPlan(
               R"({"reply":"c","actions":[{"op":"clearChat","scope":"all"}]})").ok,
          "clearChat takes no fields");
    {
      const OpPlanResult v = parseOpPlan(R"({"reply":"c","variants":[{"label":"v","actions":[)"
                                         R"({"op":"clearChat"}]}]})");
      check(v.ok && v.plan.variants.isEmpty() &&
                v.plan.warnings.value(0).startsWith("Dropped variant"),
            "clearChat inside a variant drops the variant");
      const OpPlanResult r = parseOpPlan(
          R"({"reply":"c","ask":{"question":"q?","options":[)"
          R"({"label":"a","actions":[{"op":"clearChat"}]},{"label":"b"}]}})");
      check(r.ok && r.plan.ask.options.size() == 2 &&
                r.plan.ask.options[0].actions.isEmpty(),
            "clearChat inside an ask-option preview drops the preview");
    }
    check(isEditorSettingsOp(OpKind::ClearChat) && isTopLevelOnlyOp(OpKind::ClearChat),
          "clearChat is editor-settings scoped and top-level only");
  }

  // ── §2.1 multi-image ops: `image` switches attachments, `save` persists ──
  std::printf("multi-image ops (s2.1):\n");
  {
    // The turn's attachments + the project saves, as the live target would do
    // them (MainWindow loads the attachment / creates a local project).
    struct MultiTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      int attachments = 2;   // what THIS turn attached
      QVector<int> loaded;
      QStringList saved;
      QStringList savedTo;  // the destination each save was given ("" = the editor's own store)
      stencil::core::Lines got;
      bool loadAttachment(int index, QString* err) override {
        if (index < 1 || index > attachments) {
          if (err)
            *err = QStringLiteral("this message attached %1 image(s)").arg(attachments);
          return false;
        }
        loaded << index;
        return true;
      }
      bool saveProject(const QString& name, const QString& dest, QString*) override {
        saved << name;
        savedTo << dest;   // §10: "" unless the user named a destination
        return true;
      }
      void setLayoutLines(const stencil::core::Lines& lines) override {
        got = lines;
        CanvasPlanTarget::setLayoutLines(lines);
      }
    };
    {
      const auto parsed = parseOpPlan(R"({
        "reply": "both", "actions": [
          {"op": "image", "index": 1}, {"op": "filter", "mode": "bw"},
          {"op": "save", "name": "one"},
          {"op": "image", "index": 2}, {"op": "save", "name": "two"}
        ]})");
      check(parsed.ok, "multi-image plan parses");
      MultiTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes.isEmpty(), "multi-image plan executes cleanly");
      check(target.loaded == QVector<int>({1, 2}), "each image op loaded THAT attachment");
      check(target.saved == QStringList({"one", "two"}), "one save per image, named");
    }
    {
      // An index the turn cannot satisfy costs that action, not the plan.
      const auto parsed = parseOpPlan(R"({
        "reply": "third", "actions": [
          {"op": "image", "index": 3}, {"op": "page", "format": "a5"}
        ]})");
      MultiTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "an out-of-range image index does not fail the plan");
      check(res.notes.size() == 1 && res.notes.first().contains("attached image 3") &&
                res.notes.first().contains("2 image(s)"),
            "…it lands as a skipped-action note naming the index");
      check(target.loaded.isEmpty(), "nothing was loaded");
      check(target.pageFormat == "A5", "the actions after it still ran");
    }
    {
      // Saving with nothing loaded is a skipped action too.
      const auto parsed =
          parseOpPlan(R"({"reply":"s","actions":[{"op":"save","name":"x"}]})");
      MultiTarget target(QImage(), a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "save with no image does not fail the plan");
      check(res.notes.size() == 1 && res.notes.first().contains("no working image"),
            "…it warns instead");
      check(target.saved.isEmpty(), "and nothing was saved");
    }
    {
      // A crop moves the origin; the attachment after it is a FRESH frame, so
      // the layout that follows lands on the points as written (§1 re-mapping
      // resets).
      const auto parsed = parseOpPlan(R"({
        "reply": "fresh frame", "actions": [
          {"op": "crop", "spec": {"x1": "2px", "y1": "2px"}},
          {"op": "image", "index": 1},
          {"op": "layout", "lines": [{"points": [{"x": 5, "y": 7}]}]}
        ]})");
      check(parsed.ok, "crop + image + layout plan parses");
      MultiTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "it executes");
      check(target.got.size() == 1 && target.got[0].points.size() == 1 &&
                near(target.got[0].points[0].x, 5) && near(target.got[0].points[0].y, 7),
            "switching image resets the §1 coordinate re-mapping");
    }
    {
      // A target with neither capability: the switch is a note, the save an error.
      CanvasPlanTarget target(img, a4);
      const auto p1 =
          parseOpPlan(R"({"reply":"i","actions":[{"op":"image","index":1}]})");
      const ExecResult r1 = executePlan(p1.plan, target);
      check(r1.ok && r1.notes.size() == 1, "a target that cannot switch images notes it");
      const auto p2 = parseOpPlan(R"({"reply":"s","actions":[{"op":"save"}]})");
      const ExecResult r2 = executePlan(p2.plan, target);
      check(!r2.ok && r2.error.contains("save"), "…and rejects the save outright");
    }
  }

  // ── §2 page custom dims / blank dims / formula enabled+clear ──
  std::printf("page custom / blank dims / formula forms (s2):\n");
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"p","actions":[{"op":"page","width":20,"height":30}]})");
    check(parsed.ok, "custom-dims page plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "custom-dims page executes");
    check(target.pageCustomW == 20.0 && target.pageCustomH == 30.0,
          "custom dims recorded (cm)");
    check(target.pageFormat.isEmpty(), "the format path was not taken");
    check(target.pageCm().width == 20.0 && target.pageCm().height == 30.0,
          "the page metrics adopted the custom size");
  }
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"b","actions":[{"op":"blank","color":"#123456","width":10,"height":5}]})");
    check(parsed.ok, "blank-with-dims plan parses");
    CanvasPlanTarget target(QImage(), a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "blank with explicit cm dims executes");
    const stencil::core::SizePx px =
        stencil::core::defaultBlankSizePx({10.0, 5.0}, 96.0);
    const QImage result = target.renderResult();
    check(result.width() == px.width && result.height() == px.height,
          "explicit dims size the blank (core defaultBlankSizePx)");
  }
  {
    // An empty expr clears the axis; the `enabled` form flips the toggle.
    const auto parsed = parseOpPlan(R"({
      "reply": "f", "actions": [
        {"op": "formula", "axis": "x", "expr": "x*2"},
        {"op": "formula", "axis": "x", "expr": ""},
        {"op": "formula", "enabled": false}
      ]})");
    check(parsed.ok, "formula clear + enabled plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "formula clear + enabled executes");
    check(target.formulaX.isEmpty(), "the empty expr cleared the x axis");
    check(target.formulasEnabled == 0, "enabled:false switched formulas OFF");
  }

  // ── §2 undo / redo ──
  std::printf("undo/redo (s2):\n");
  {
    // A recorder with a bounded history: the executor asks for the steps, the
    // target reports how many actually ran, and the shortfall becomes a note.
    struct HistoryTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      int undoAvailable = 1, redoAvailable = 0;
      QStringList calls;
      int stepHistory(bool redo, int steps) override {
        int& avail = redo ? redoAvailable : undoAvailable;
        const int done = std::min(avail, steps);
        avail -= done;
        calls << QStringLiteral("%1:%2").arg(redo ? "redo" : "undo").arg(done);
        return done;
      }
    };
    {
      const auto parsed =
          parseOpPlan(R"({"reply":"u","actions":[{"op":"undo","steps":3}]})");
      check(parsed.ok, "undo steps=3 parses");
      HistoryTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.calls == QStringList{"undo:1"},
            "undo reached the target's history");
      check(res.notes.size() == 1 &&
                res.notes.first() == "undo: only 1 of 3 step(s) available",
            "running out of steps is a note, never a failed plan");
    }
    {
      const auto parsed = parseOpPlan(R"({"reply":"r","actions":[{"op":"redo"}]})");
      HistoryTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.calls == QStringList{"redo:0"}, "redo defaults to one step");
      check(res.notes.size() == 1 && res.notes.first() == "redo: nothing to redo",
            "an empty history is the nothing-to-redo note");
    }
    {
      // The default CanvasPlanTarget drives the real canvas history stack —
      // empty here, so one undo lands as the nothing-to-undo note.
      const auto parsed = parseOpPlan(R"({"reply":"u","actions":[{"op":"undo"}]})");
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes.size() == 1 && res.notes.first().contains("nothing to undo"),
            "the canvas target's empty history notes, not fails");
    }
    {
      // A surface with NO history at all (the base default) fails the plan.
      struct NoHistoryTarget : CanvasPlanTarget {
        using CanvasPlanTarget::CanvasPlanTarget;
        int stepHistory(bool redo, int steps) override {
          return PlanTarget::stepHistory(redo, steps);
        }
      };
      NoHistoryTarget target(img, a4);
      const ExecResult res = executePlan(
          parseOpPlan(R"({"reply":"u","actions":[{"op":"undo"}]})").plan, target);
      check(!res.ok && res.error.contains("history is not available"),
            "a history-less surface rejects undo outright");
    }
  }

  // ── §10 compare / zoom (view-only) ──
  std::printf("compare/zoom (s10):\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "v", "actions": [
        {"op": "compare", "mode": "vertical", "split": 0.25},
        {"op": "zoom", "percent": 150}
      ]})");
    check(parsed.ok, "compare + zoom plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "compare + zoom execute");
    check(target.compareMode == "vertical" && target.compareSplit == 0.25,
          "compare mode + split recorded");
    check(target.zoomPercent == 150 && !target.zoomFit, "zoom percent recorded");
    check(target.renderResult().size() == img.size(),
          "view ops never touch the working image");
  }
  {
    const auto parsed = parseOpPlan(R"({"reply":"z","actions":[{"op":"zoom","fit":true}]})");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && target.zoomFit, "zoom fit:true reaches the fit path");
  }
  {
    // Vertical, then a follow-up "none" that ECHOES the divider back (models do):
    // the split view clears — the echoed field is ignored, never a failed plan.
    CanvasPlanTarget target(img, a4);
    const ExecResult on = executePlan(
        parseOpPlan(R"({"reply":"v","actions":[
          {"op":"compare","mode":"vertical","split":0.3}]})").plan, target);
    const ExecResult off = executePlan(
        parseOpPlan(R"({"reply":"n","actions":[
          {"op":"compare","mode":"none","split":0.3}]})").plan, target);
    check(on.ok && off.ok, "compare vertical then none both execute");
    check(target.compareMode == "none", "compare none clears the split view");
  }

  // ── §10 lineStyle widening: pointColor / drawMode apply, fillColor is a
  //    BROWSER control — the desktop notes+skips that field ──
  std::printf("lineStyle widening (s10):\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "ls", "actions": [{"op": "lineStyle",
        "color": "#00ff00", "pointColor": "", "drawMode": "rect",
        "fillColor": "#112233"}]})");
    check(parsed.ok, "widened lineStyle parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "widened lineStyle executes");
    check(target.lsColor == "#00ff00", "stroke colour still applied");
    check(target.lsPointColorSet && target.lsPointColor.isEmpty(),
          "pointColor \"\" recorded as the explicit follow-the-stroke value");
    check(target.lsDrawMode == "rect", "drawMode applied");
    check(res.notes.size() == 1 &&
              res.notes.first().contains("fillColor is a browser-editor control"),
          "fillColor lands as the desktop's note+skip");
  }
  {
    // fillColor alone: nothing to apply here, but the note still says why.
    const auto parsed = parseOpPlan(
        R"({"reply":"ls","actions":[{"op":"lineStyle","fillColor":"transparent"}]})");
    check(parsed.ok, "fillColor-only lineStyle parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.notes.size() == 1 &&
              res.notes.first().contains("browser-editor control"),
          "fillColor-only lineStyle is a pure note");
    check(target.lsColor.isEmpty() && !target.lsPointColorSet,
          "…and applied nothing else");
  }

  // ── §10 accent preset / copy what ──
  std::printf("accent preset / copy what (s10):\n");
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"a","actions":[{"op":"accent","preset":"green"}]})");
    check(parsed.ok, "accent preset parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && target.accentPreset == "green" && target.accentColor.isEmpty(),
          "the preset form takes the preset path, not the hex one");
  }
  {
    // An unknown preset name is the target's note+skip (contract §10).
    struct PresetTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      void setAccentPreset(const QString& preset, QString* note) override {
        if (preset != "green") *note = QStringLiteral("unknown accent preset \"%1\"").arg(preset);
        else accentPreset = preset;
      }
    };
    PresetTarget target(img, a4);
    const ExecResult res = executePlan(
        parseOpPlan(R"({"reply":"a","actions":[{"op":"accent","preset":"sparkle"}]})").plan,
        target);
    check(res.ok && res.notes.size() == 1 && res.notes.first().contains("unknown accent preset"),
          "an unknown preset is a note + skip, never a failed plan");
  }
  {
    // copy what:"layout" needs drawn lines: a note without them, the layout
    // clipboard path with them.
    const auto parsed = parseOpPlan(
        R"({"reply":"c","actions":[{"op":"copy","what":"layout"}]})");
    check(parsed.ok, "copy what:layout parses");
    {
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && !target.copiedLayout, "no drawn lines: nothing copied");
      check(res.notes.size() == 1 && res.notes.first().contains("no drawn lines"),
            "…and the skip is a note");
    }
    {
      CanvasPlanTarget target(img, a4);
      const auto both = parseOpPlan(R"({
        "reply": "c", "actions": [
          {"op": "layout", "lines": [{"points": [{"x": 1, "y": 1}, {"x": 5, "y": 5}]}]},
          {"op": "copy", "what": "layout"}
        ]})");
      const ExecResult res = executePlan(both.plan, target);
      check(res.ok && target.copiedLayout && !target.copied,
            "with lines drawn, copy what:layout takes the layout path only");
    }
  }

  // ── §10 blankColor: blanks only (note+skip), keeps the drawn lines ──
  std::printf("blankColor (s10):\n");
  {
    // On a NON-blank image the recolour is a note + skip, and pixels survive.
    const auto parsed = parseOpPlan(
        R"({"reply":"b","actions":[{"op":"blankColor","color":"#ff0000"}]})");
    check(parsed.ok, "blankColor plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "blankColor on a photo does not fail the plan");
    check(res.notes.size() == 1 &&
              res.notes.first().contains("only a blank"),
          "…it lands as the blanks-only note");
    const QColor kept = target.renderResult().pixelColor(8, 6);
    check(kept.red() != 255 || kept.green() != 0, "the photo's pixels were left alone");
  }
  {
    // On a blank it recolours in place and KEEPS the drawn lines.
    const auto parsed = parseOpPlan(R"({
      "reply": "b", "actions": [
        {"op": "blank", "color": "red", "format": "a10"},
        {"op": "layout", "lines": [{"points": [{"x": 10, "y": 70}, {"x": 90, "y": 70}],
                                    "color": "#00FF00", "thickness": 3}]},
        {"op": "blankColor", "color": "#0000ff"}
      ]})");
    check(parsed.ok, "blank + layout + blankColor plan parses");
    CanvasPlanTarget target(QImage(), a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.notes.isEmpty(), "recolouring a blank is not a note");
    const QImage result = target.renderResult();
    const QColor bg = result.pixelColor(50, 20);
    check(bg.blue() == 255 && bg.red() == 0, "the background recoloured to blue");
    const QColor line = result.pixelColor(50, 70);
    check(line.green() > 200 && line.blue() < 100, "…and the drawn line survived");
  }

  // ── §10 removeProject current:true + the project-row targets ──
  std::printf("project rows (s10):\n");
  {
    // Records the ORDER a plan's settings ops and its (deferred) dialog run in.
    struct DialogTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList calls;
      void setTheme(const QString& mode) override { calls << QStringLiteral("theme:%1").arg(mode); }
      bool openDialog(const QString& name, QString* note) override {
        calls << (name.isEmpty() ? QStringLiteral("dialog:<close>")
                                 : QStringLiteral("dialog:%1").arg(name));
        *note = QString();
        return true;
      }
    };
    struct ProjectsTarget2 : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList calls;
      bool removeProjectNamed(const QString& name, bool current, QString* note) override {
        calls << (current ? QStringLiteral("remove:<current>")
                          : QStringLiteral("remove:%1").arg(name));
        *note = QString();
        return true;
      }
      bool renameActiveProject(const QString& name, QString* note) override {
        calls << QStringLiteral("rename:%1").arg(name);
        *note = QString();
        return true;
      }
      bool setProjectColor(const QString& color, QString* note) override {
        calls << QStringLiteral("color:%1").arg(color);
        *note = QString();
        return true;
      }
      bool openDialog(const QString& name, QString* note) override {
        calls << (name.isEmpty() ? QStringLiteral("dialog:<close>")
                                 : QStringLiteral("dialog:%1").arg(name));
        *note = QString();
        return true;
      }
      bool openProjectNamed(const QString& name, bool last, QString* note) override {
        calls << (last ? QStringLiteral("open:last") : QStringLiteral("open:%1").arg(name));
        *note = last ? QStringLiteral("there are no saved projects yet")
                     : QStringLiteral("no saved project named \"%1\"").arg(name);
        return true;
      }
      bool setIncognito(bool on, QString* note) override {
        calls << QStringLiteral("incognito:%1").arg(on ? "on" : "off");
        *note = QStringLiteral("incognito can only be toggled on a blank editor");
        return true;
      }
    };
    const auto parsed = parseOpPlan(R"({
      "reply": "p", "actions": [
        {"op": "removeProject", "current": true},
        {"op": "renameProject", "name": "portrait 2"},
        {"op": "projectColor", "color": ""},
        {"op": "openProject", "name": "missing"},
        {"op": "incognito", "on": true}
      ]})");
    check(parsed.ok, "project-row plan parses");
    ProjectsTarget2 target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "project-row plan executes");
    check(target.calls == QStringList({"remove:<current>", "rename:portrait 2",
                                       "color:", "open:missing", "incognito:on"}),
          "each op reached its own flow, in order (projectColor \"\" = clear)");
    check(res.notes.size() == 2 && res.notes.at(0).startsWith("openProject:") &&
              res.notes.at(1).startsWith("incognito:"),
          "the unknown-name and non-blank skips surface as notes");

    // §10 openProject's other form: "the last project I worked on" reaches the target
    // as last=true, with no name for the model to have guessed.
    ProjectsTarget2 lastTarget(img, a4);
    const auto lastPlan = parseOpPlan(
        R"({"reply":"p","actions":[{"op":"openProject","last":true}]})");
    check(lastPlan.ok, "openProject last:true parses");
    check(executePlan(lastPlan.plan, lastTarget).ok, "…and executes");
    check(lastTarget.calls == QStringList({"open:last"}), "…as the LAST-project form");
    // Exactly one form: a name AND last is a parse error, so nothing runs.
    check(!parseOpPlan(R"({"reply":"p","actions":[{"op":"openProject","name":"a","last":true}]})").ok,
          "name + last is refused at the parser");
    check(!parseOpPlan(R"({"reply":"p","actions":[{"op":"openProject","last":false}]})").ok,
          "…and last:false says nothing, so it is refused too");

    // §10 clearProjects keepCurrent: "delete the others" reaches the target as the
    // spare-the-open-one form, never as a clear-everything-and-save-it-back dance.
    check(parseOpPlan(R"({"reply":"p","actions":[{"op":"clearProjects","keepCurrent":true}]})").ok,
          "clearProjects keepCurrent:true parses");
    check(!parseOpPlan(R"({"reply":"p","actions":[{"op":"clearProjects","keepCurrent":false}]})").ok,
          "…and keepCurrent:false says nothing, so it is refused");

    // §10 dialog: DEFERRED past the other actions, and only the LAST one asked for runs
    // ("close this window and open that one" must end with that one open).
    DialogTarget dlg(img, a4);
    const auto dialogPlan = parseOpPlan(R"({
      "reply": "p", "actions": [
        {"op": "dialog", "close": true},
        {"op": "theme", "mode": "dark"},
        {"op": "dialog", "name": "servers"}
      ]})");
    check(dialogPlan.ok, "a dialog plan parses");
    check(executePlan(dialogPlan.plan, dlg).ok, "…and executes");
    check(dlg.calls == QStringList({"theme:dark", "dialog:servers"}),
          "the window opens LAST, and only the last one asked for");
    check(!parseOpPlan(R"({"reply":"p","actions":[{"op":"dialog","name":"llm"}]})").ok,
          "the assistant's own settings window is not a name it can ask for");
    check(!parseOpPlan(R"({"reply":"p","actions":[{"op":"dialog"}]})").ok,
          "…and a dialog naming nothing is refused");
  }
  {
    // The base target has none of the project rows — typed failures each.
    CanvasPlanTarget target(img, a4);
    const char* plans[] = {
        R"({"reply":"p","actions":[{"op":"renameProject","name":"a"}]})",
        R"({"reply":"p","actions":[{"op":"projectColor","color":"#112233"}]})",
        R"({"reply":"p","actions":[{"op":"openProject","name":"a"}]})",
        R"({"reply":"p","actions":[{"op":"incognito","on":true}]})",
    };
    bool allRejected = true;
    for (const char* json : plans)
      if (executePlan(parseOpPlan(QString::fromUtf8(json)).plan, target).ok)
        allRejected = false;
    check(allRejected, "a surface without the project rows rejects each op");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

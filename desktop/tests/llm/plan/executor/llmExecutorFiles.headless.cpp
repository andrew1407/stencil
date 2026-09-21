// Reaching the outside world: openUrl, openFile and the save destination.
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkFileOps(const QImage& img, const stencil::core::PageSize& a4) {
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
        R"({"reply":"o","actions":[{"op":"openUrl","url":"https://a.example/cat.jpg","incognito":true}]})");
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
      target.typed = "please open https://a.example/cat.jpg in incognito";
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "echoed openUrl executes");
      check(target.opened == QStringList{"https://a.example/cat.jpg#incognito"},
            "loader got the URL + incognito");
    }
    {
      // The base PlanTarget has no loader — a typed failure, not a crash.
      CanvasPlanTarget target(img, a4);
      const auto p2 = parseOpPlan(
          R"({"reply":"o","actions":[{"op":"openUrl","url":"https://a.example/cat.jpg"}]})");
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
               R"({"reply":"o","actions":[{"op":"openFile","path":"https://a.example/cat.png"}]})")
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

  }

}  // namespace llmexec

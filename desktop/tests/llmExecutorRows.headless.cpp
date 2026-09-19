// The project rows a surface must supply, and the typed failures without them.
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkProjectRows(const QImage& img, const stencil::core::PageSize& a4) {
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

  }

}  // namespace llmexec

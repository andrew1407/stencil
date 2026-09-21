// The op registry behind the system prompt (§13): every op carries its own phrase.
#include "llmClientParts.hpp"

namespace llmclient {

  void checkOpRegistry() {
  // ── §13 op registry: name set, flags, key phrases (instead of block bytes) ──
  std::printf("op registry (contract s13):\n");
  {
    // (a) The registered op-name set IS the contract's desktop surface: the
    // §2 core/history/multi-image ops plus the §10 editor-settings ops.
    QSet<QString> names;
    for (const OpDescriptor& e : opRegistry()) names.insert(QString::fromUtf8(e.name));
    const QSet<QString> expected = {
        // §2 core image ops
        "crop", "rotate", "filter", "layout", "formula", "page", "blank", "frame",
        // §2 history + §2.1 multi-image ops
        "undo", "redo", "image", "save",
        // §10 editor-settings ops
        "theme", "accent", "lineStyle", "units", "view", "clear", "openUrl", "openFile",
        "connect", "disconnect", "copy", "removeProject", "clearProjects",
        "compare", "zoom", "renameProject", "projectColor", "blankColor",
        "openProject", "incognito", "chatPanel", "dialog", "clearChat"};
    check(names == expected, "registry op-name set == the contract's desktop surface");
    check(opRegistry().size() == 35, "one registry entry per OpKind (35)");

    // A KNOWN op with an unknown field fails the plan while an UNKNOWN op is only skipped with
    // a warning, so every registered name must be known to the opPlan parser.
    bool allKnown = true;
    for (const OpDescriptor& e : opRegistry()) {
      const QString plan = QStringLiteral(
                               "{\"reply\":\"r\",\"actions\":[{\"op\":\"%1\","
                               "\"notAContractField\":1}]}")
                               .arg(QString::fromUtf8(e.name));
      const OpPlanResult r = parseOpPlan(plan);
      if (r.ok || r.plan.chatOnly) allKnown = false;
    }
    check(allKnown, "every registered op name is KNOWN to the opPlan parser");

    // (b) Flags: each entry's scope/topLevelOnly/history flags match the
    // opPlan helpers the parser and executor actually enforce with.
    bool flagsOk = true;
    for (const OpDescriptor& e : opRegistry()) {
      if (e.editorSettings != isEditorSettingsOp(e.kind)) flagsOk = false;
      if (e.topLevelOnly != isTopLevelOnlyOp(e.kind)) flagsOk = false;
      if (e.history != isHistoryOp(e.kind)) flagsOk = false;
    }
    check(flagsOk, "registry flags match the opPlan scope helpers");

    // (c) One key semantic phrase per bullet (contract §4/§10 spec).
    const QVector<QPair<QString, QString>> phrases = {
        {"crop", "move edges inward"},
        {"rotate", "quarter turns only"},
        {"filter", "\"custom\" is a duotone tint and requires \"tint\""},
        {"layout", "array REMOVES every drawn line"},
        {"formula", "{\"op\":\"formula\",\"enabled\":false} switches formulas OFF"},
        {"page", "{\"op\":\"page\",\"width\":20,\"height\":30} in centimetres"},
        {"blank", "centimetre dims ride as \"width\"/\"height\" instead of \"format\""},
        {"undo", "\"Undo that\" means {\"op\":\"undo\"}"},
        {"redo", "{\"op\":\"redo\",\"steps\":1}"},
        {"frame", "only valid when the current input is a video"},
        {"image", "1-based, in attachment order"},
        {"save", "ONLY a path the user themselves wrote"},
        {"theme", "A COLOUR (\"make the theme cyan\") is the accent"},
        {"accent", "translate colour names yourself"},
        {"lineStyle", "change the DEFAULT style for new lines"},
        {"units", "display units"},
        {"view", "show or hide points and lines"},
        {"clear", "REMOVE the working image and its lines"},
        {"openUrl", "ONLY a URL the user themselves wrote"},
        {"openFile", "guess or list one, and never a directory"},
        {"connect", "never invent or suggest a new address"},
        {"disconnect", "{\"op\":\"disconnect\",\"server\":"},
        {"copy", "copy the current rendered image to the system clipboard"},
        {"removeProject", "remove ONE saved local project by its"},
        {"clearProjects", "remove EVERY saved local project"},
        {"compare", "the exported image is unchanged"},
        {"zoom", "This never changes the picture"},
        {"renameProject", "rename the active saved project"},
        {"projectColor", "the project's name colour"},
        {"blankColor", "KEEPING the drawn lines"},
        {"openProject", "open a saved local project into the editor"},
        {"incognito", "edit without saving; only togglable on a blank editor"},
        {"chatPanel", "a \"dock\" on its own opens the panel where it lands"},
        {"dialog", "\"visuals\" (style & visual settings) or \"help\""},
        {"clearChat", "the clear happens after this plan's other actions finish"},
    };
    bool phrasesOk = true;
    for (const auto& p : phrases) {
      bool found = false;
      for (const OpDescriptor& e : opRegistry())
        if (p.first == QLatin1String(e.name) &&
            e.bullet.contains(p.second))
          found = true;
      if (!found) {
        std::printf("    missing phrase for op %s\n", qPrintable(p.first));
        phrasesOk = false;
      }
    }
    check(phrasesOk && phrases.size() == 35, "every bullet carries its key phrase");

    // The §10 also-accepts widenings ride as addenda on their ops.
    const QVector<QPair<OpKind, QString>> addendaPhrases = {
        {OpKind::REMOVE_PROJECT, "{\"op\":\"removeProject\",\"current\":true}"},
        {OpKind::COPY, "{\"op\":\"copy\",\"what\":\"layout\"}"},
        {OpKind::ACCENT, "{\"op\":\"accent\",\"preset\":\"green\"}"},
        {OpKind::LINE_STYLE, "\"fillColor\" for the defaults of NEW lines"},
        {OpKind::OPEN_PROJECT, "{\"op\":\"openProject\",\"last\":true}"},
    };
    bool addOk = opAddenda().size() == addendaPhrases.size();
    for (const auto& p : addendaPhrases) {
      bool found = false;
      for (const OpAddendum& ad : opAddenda())
        if (ad.kind == p.first && ad.bullet.contains(p.second))
          found = true;
      if (!found) addOk = false;
    }
    check(addOk, "the five also-accepts widenings are registered as addenda");
  }

  }

}  // namespace llmclient

// §13 byte-stability: the assembled ops block against an independent legacy copy.
#include "llmClientParts.hpp"

namespace llmclient {

// §13 byte-stability transition proof: the pre-registry hand-embedded op
// BULLETS, kept HERE (test-only) so assembly order/joins are proven against an
// independent copy. The §4 prose head/tail load from the SAME qrc canon
// opRegistry reads (:/config/llm/systemPrompt.json) — no second prompt-prose
// literal exists anywhere.
static const char LEGACY_CORE_OPS_BLOCK[] =
    R"__(- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
  opposite side. Include only the edges you want to move. For a target aspect ratio add
  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
  region should be kept.
- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.
- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.
- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
  shapes, or structure from an attached image, answer with this op. An empty "lines"
  array REMOVES every drawn line — that is what "clear/remove the lines" means.
- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.
- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).
- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
  centimetre dims ride as "width"/"height" instead of "format".
- {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
  "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
  than one request.
- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
  only valid when the current input is a video.
- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
  message (1-based, in attachment order); coordinates in later actions are in THAT
  image's pixel frame. Only valid when the user attached images. Use it to edit several
  attached images in one plan, giving each image its OWN actions.
- {"op":"save","name":"portrait 1","path":"~/Downloads"} — save the current image with its
  drawn lines. "path" is optional and may be a folder or a file name (".stencil" saves the
  whole project, an image extension saves the picture); with no path it becomes a project in
  the editor. ONLY a path the user themselves wrote in this conversation — never invent,
  complete or rewrite one. When the user asks to process several images and keep the results,
  finish each image's actions with a "save" before switching to the next: image 1, its edits,
  save, image 2, its edits, save, …)__";

static const char LEGACY_EDITOR_OPS_BLOCK[] =
    R"__(- {"op":"theme","mode":"light"|"dark"} — switch the editor between light and dark
  ONLY; "mode" takes no other value. A COLOUR ("make the theme cyan") is the accent
  op below, never this one.
- {"op":"accent","color":"#7c3aed"} — set the editor accent colour. "color" must be
  a #rrggbb hex, so translate colour names yourself (cyan = "#00ffff").
- {"op":"lineStyle","color":"#00ff00","thickness":3,"pointSize":6,"style":"dashed"} —
  change the DEFAULT style for new lines (any subset of fields).
- {"op":"units","value":"cm"|"in"} — display units.
- {"op":"view","points":true,"lines":false} — show or hide points and lines.
- {"op":"clear"} — REMOVE the working image and its lines, leaving the editor empty.
  This is what "remove/delete/clear the image" means. Never answer that with
  {"op":"blank"}: a blank REPLACES the picture with a white page, which is not a
  removal. Takes no fields.
- {"op":"openUrl","url":"https://…","incognito":false} — load an image (or video
  frame) from a URL into the editor; "incognito": true loads it into THIS editor
  switched to incognito (nothing is saved), never a second tab or window, so the
  rest of your plan keeps acting on it. ONLY a URL the user themselves wrote in
  this conversation — never introduce, complete, or rewrite one.
- {"op":"openFile","path":"~/Pictures/portrait.png"} — load a LOCAL file the user named
  into the editor: an image or video, a layout ".json" (drawn onto the current picture), or a
  ".stencil" project. ONLY a path the user themselves wrote in this conversation — never
  invent, complete, guess or list one, and never a directory.
- {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
  user's collaboration-server connections. Only a server the user has already saved
  may be named — never invent or suggest a new address. These editor ops are not
  image edits and cannot appear inside "variants".
- {"op":"copy"} — copy the current rendered image to the system clipboard. This IS
  what "copy the result / copy to clipboard" means; never answer that it cannot be
  done. Takes no fields.
- {"op":"removeProject","name":"portrait 1"} — remove ONE saved local project by its
  name; the app asks the user to confirm before anything is deleted.
- {"op":"clearProjects"} — remove EVERY saved local project. This IS what "clear/
  delete my projects" means; the app asks the user to confirm first. Server-stored
  projects are never touched from chat. {"op":"clearProjects","keepCurrent":true} spares
  the project that is open right now — that IS "delete the others / all but this one",
  and you must never clear everything and try to save it back instead.
- {"op":"compare","mode":"none"|"original"|"vertical"|"horizontal","split":0.5} — the
  comparison view: the original beside/over the edit ("vertical" = side-by-side split).
  View-only; the exported image is unchanged.
- {"op":"zoom","percent":150} or {"op":"zoom","fit":true} — zoom the USER'S VIEW (or
  fit to the window). This never changes the picture — cropping is the crop op.
- {"op":"renameProject","name":"…"} — rename the active saved project.
- {"op":"projectColor","color":"#ec4899"} — the project's name colour ("" = theme).
- {"op":"blankColor","color":"#dbeafe"} — recolour a BLANK project's background,
  KEEPING the drawn lines. "Recolour/change the background" means THIS, never a new
  {"op":"blank"} (that replaces the page and destroys the lines).
- {"op":"openProject","name":"…"} — open a saved local project into the editor (the
  app confirms first when unsaved work would be replaced).
- {"op":"incognito","on":true} — edit without saving; only togglable on a blank editor.
- {"op":"chatPanel","open":true,"dock":"right"} — show, hide or move THIS assistant
  panel: "dock" is "left"|"right"|"top"|"bottom"|"float" ("float" = a free-standing
  window), and a "dock" on its own opens the panel where it lands. At least one field.
- {"op":"dialog","name":"projects"} — open one of the editor's own windows for the
  user: "projects" (the saved projects list), "servers" (connections), "shortcuts",
  "visuals" (style & visual settings) or "help". {"op":"dialog","close":true} closes the
  open one. Use it when the user asks to SEE or MANAGE something by hand; when they ask
  for a change you can make yourself, make it instead.
- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
  confirm first, and the clear happens after this plan's other actions finish. This IS
  what "clear the chat / conversation / history" means; never answer that it cannot be
  done. Takes no fields.
- "removeProject" also accepts {"op":"removeProject","current":true} — remove the
  project that is open right now (confirmed in-app).
- "copy" also accepts {"op":"copy","what":"layout"} — the layout JSON instead of the
  image.
- "accent" also accepts {"op":"accent","preset":"green"} — a named preset persists and
  syncs; use a preset when the user names a colour that has one.
- "lineStyle" also carries "pointColor" ("" = follow the stroke), "drawMode"
  ("line"|"rect") and "fillColor" for the defaults of NEW lines.
- "openProject" also accepts {"op":"openProject","last":true} — the project edited
  most recently, which is what "the last project" / "the one I worked on last" means.
  The app resolves it; you never see the list, so never ask which one that is.)__";

  void checkPromptAssembly() {
  // ── §13 byte-stability: assembly reproduces the pre-registry constants ──
  std::printf("assembly byte-stability:\n");
  {
    // The old hand-embedded path: qrc prose head + core bullets + the §10
    // block spliced after the frame bullet + qrc prose tail. Head/tail come
    // from the canon asset; the bullet copies above stay independent.
    QFile pf(QStringLiteral(":/config/llm/systemPrompt.json"));
    check(pf.open(QIODevice::ReadOnly), "prompt canon qrc alias resolves");
    const QJsonObject prose = QJsonDocument::fromJson(pf.readAll()).object();
    QString legacy = prose.value("head").toString() +
                     QString::fromUtf8(LEGACY_CORE_OPS_BLOCK) +
                     prose.value("tail").toString();
    const QString anchor = QStringLiteral("only valid when the current input is a video.");
    const int at = legacy.indexOf(anchor);
    check(at >= 0, "prompt canon head/tail parse (frame anchor found)");
    legacy.insert(at + anchor.size(),
                  QStringLiteral("\n") + QString::fromUtf8(LEGACY_EDITOR_OPS_BLOCK));
    check(LlmClient::systemPrompt(QString()) == legacy,
          "assembled system prompt is byte-identical to the legacy constants");
    check(assembleEditorOpsBlock() == QString::fromUtf8(LEGACY_EDITOR_OPS_BLOCK),
          "assembled s10 editor block is byte-identical to the legacy constant");
    check(LlmClient::systemPrompt("ctx") == legacy + "\n\nctx",
          "dynamic suffix still appends after the assembled prompt");
  }

  }

}  // namespace llmclient

// The editor-settings ops — theme, accent, units, view, zoom, connections, projects, chat panel and
// incognito. None is allowed inside a variant, and each rides §10's true+note contract.
#include "planExecutorParts.hpp"

namespace stencil::llm::exec {

  bool applyStateAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                       QStringList* notes, bool* handled, QString* err) {
    *handled = true;
    switch (a.op) {
        case OpKind::CLEAR_CHAT:
        case OpKind::DIALOG:
          // executePlan defers these past every other action; reaching here
          // means a variant/preview slipped through the parse ban.
          *err = QStringLiteral("editor-settings ops are not allowed inside a variant");
          return false;
        // §10 editor-settings ops (parse-banned in variants; the inVariant
        // guard below is defensive only)
        case OpKind::THEME:
        case OpKind::ACCENT:
        case OpKind::LINE_STYLE:
        case OpKind::UNITS:
        case OpKind::VIEW:
        case OpKind::CLEAR:
        case OpKind::CONNECT:
        case OpKind::DISCONNECT:
        case OpKind::COPY:
        case OpKind::REMOVE_PROJECT:
        case OpKind::CLEAR_PROJECTS:
        case OpKind::COMPARE:
        case OpKind::ZOOM:
        case OpKind::RENAME_PROJECT:
        case OpKind::PROJECT_COLOR:
        case OpKind::BLANK_COLOR:
        case OpKind::OPEN_PROJECT:
        case OpKind::CHAT_PANEL:
        case OpKind::INCOGNITO: {
          if (inVariant) {
            *err = QStringLiteral("editor-settings ops are not allowed inside a variant");
            return false;
          }
          // §10: the true+note contract - a non-empty note (no active project, non-blank editor,
          // unknown/ambiguous name, declined confirm) is surfaced and the plan continues; false fails it.
          const auto noted = [&](const char* opName, bool ok, const QString& note) {
            if (!ok) { *err = note; return false; }
            if (!note.isEmpty() && notes)
              *notes << QStringLiteral("%1: %2").arg(QLatin1String(opName), note);
            return true;
          };
          switch (a.op) {
            case OpKind::THEME: target.setTheme(a.mode); return true;
            case OpKind::ACCENT: {
              // §10: a PRESET persists and syncs; unknown preset names are the
              // target's note+skip. A raw hex keeps the current behaviour.
              if (!a.preset.isEmpty()) {
                QString note;
                target.setAccentPreset(a.preset, &note);
                if (!note.isEmpty() && notes)
                  *notes << QStringLiteral("accent: %1").arg(note);
                return true;
              }
              target.setAccent(a.color);
              return true;
            }
            case OpKind::LINE_STYLE:
              // §10: fillColor is a BROWSER-only control — this editor notes
              // and skips that field, applying the rest.
              if (!a.fillColor.isEmpty() && notes)
                *notes << QStringLiteral(
                    "lineStyle: fillColor is a browser-editor control — skipped here");
              target.setDefaultLineStyle(a);
              return true;
            case OpKind::UNITS: target.setUnits(a.value); return true;
            case OpKind::VIEW: target.setViewVisibility(a.viewPoints, a.viewLines); return true;
            case OpKind::CLEAR: target.clearImage(); frame.reset(); return true;
            case OpKind::CONNECT: return target.connectServer(a.server, err);
            case OpKind::COPY:
              // §10: copy what:"layout" needs drawn lines; the default image form needs a working image -
              // both are a note+skip, never a failed plan.
              if (a.what == QLatin1String("layout")) {
                if (!target.hasDrawnLines()) {
                  if (notes)
                    *notes << QStringLiteral("Skipped copy — no drawn lines to copy");
                  return true;
                }
                return target.copyLayout(err);
              }
              if (!target.hasImage()) {
                if (notes) *notes << QStringLiteral("Skipped copy — no working image to copy");
                return true;
              }
              return target.copyImage(err);
            case OpKind::REMOVE_PROJECT: {
              QString note;
              return noted("removeProject",
                           target.removeProjectNamed(a.name, a.current, &note), note);
            }
            case OpKind::CLEAR_PROJECTS: {
              QString note;
              return noted("clearProjects", target.clearProjects(a.current, &note), note);
            }
            case OpKind::COMPARE:
              return target.setCompare(a.mode, a.split, err);
            case OpKind::ZOOM:
              return target.setZoom(a.percent, a.fit, err);
            case OpKind::RENAME_PROJECT: {
              QString note;
              return noted("renameProject", target.renameActiveProject(a.name, &note),
                           note);
            }
            case OpKind::PROJECT_COLOR: {
              QString note;
              return noted("projectColor", target.setProjectColor(a.color, &note), note);
            }
            case OpKind::BLANK_COLOR: {
              QString note;
              return noted("blankColor", target.setBlankColor(a.color, &note), note);
            }
            case OpKind::OPEN_PROJECT: {
              QString note;
              if (!noted("openProject", target.openProjectNamed(a.name, a.current, &note), note))
                return false;
              // An actually opened project is a fresh working image, so a fresh frame (a noted skip left the
              // canvas alone, where the reset is harmless anyway).
              if (note.isEmpty()) frame.reset();
              return true;
            }
            case OpKind::INCOGNITO: {
              QString note;
              return noted("incognito", target.setIncognito(a.incognito, &note), note);
            }
            case OpKind::CHAT_PANEL: {
              QString note;
              return noted("chatPanel", target.setChatPlacement(a.chatOpen, a.dock, &note), note);
            }
            default: return target.disconnectServer(a.server, err);
          }
        }
      default: break;
    }
    *handled = false;
    return false;
  }

}  // namespace stencil::llm::exec

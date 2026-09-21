// Headless check of app/ProjectNameBar.hpp — which chips the header row's project-name
// group shows in each state. The rule is browser parity (the topbar name field): ✓/✗ only
// while renaming, ✎/🎨 only outside it and only inked on hover — but always holding their
// slots, because taking the slots away shoved the "?" beside them sideways. Pure state;
// no widgets, no display.
#include "ProjectNameBar.hpp"

#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::ProjectNameBar;

int main() {
  // ── At rest on an editable project: the affordances hold their slots, unhovered.
  {
    const auto c = ProjectNameBar::chipsFor(/*editable=*/true, false, /*hover=*/false, false);
    check(!c.marks, "nothing is being renamed, so no ✓/✗");
    check(c.affordances, "the ✎/🎨 pair keeps its slots even unhovered");
    check(!c.affordancesPainted, "…but is painted out until the cursor arrives");
    check(!c.blankSwatch, "a project with a picture has no blank fill to recolour");
  }

  // ── Hovering the group inks the pair without moving anything.
  {
    const auto c = ProjectNameBar::chipsFor(true, false, /*hover=*/true, false);
    check(c.affordances && c.affordancesPainted, "hover inks the pair in the slots it already had");
    check(!c.marks, "hover alone never brings up the rename marks");
  }

  // ── Renaming: the marks replace the pair, hover or not.
  {
    const auto c = ProjectNameBar::chipsFor(true, /*editing=*/true, false, false);
    check(c.marks, "edit mode shows ✓/✗");
    check(!c.affordances && !c.affordancesPainted, "…and takes the ✎/🎨 slots back");
    const auto hovered = ProjectNameBar::chipsFor(true, true, /*hover=*/true, false);
    check(hovered.marks && !hovered.affordances, "hovering mid-rename changes nothing");
  }

  // ── A read-only name (a server project the session cannot rename, an empty editor):
  // no affordances at all, hovered or not.
  {
    const auto c = ProjectNameBar::chipsFor(/*editable=*/false, false, true, false);
    check(!c.affordances && !c.affordancesPainted, "an uneditable name offers no ✎/🎨");
    check(!c.marks, "and nothing to accept or cancel");
  }

  // ── The blank-fill swatch is gated on the fill, not on whether the project is saved —
  // an unsaved blank is recoloured in memory — but it stands aside while renaming.
  {
    check(ProjectNameBar::chipsFor(true, false, false, /*blank=*/true).blankSwatch,
          "a blank project shows its fill swatch");
    check(ProjectNameBar::chipsFor(/*editable=*/false, false, false, true).blankSwatch,
          "…even when the name itself cannot be edited");
    check(!ProjectNameBar::chipsFor(true, /*editing=*/true, false, true).blankSwatch,
          "…but not while the row is showing the rename marks");
  }

  // ── The instance form reads its own editing/hover state.
  {
    ProjectNameBar bar;
    check(!bar.editing && !bar.hover, "a fresh bar is at rest");
    check(!bar.field && !bar.group && !bar.accept, "and owns no widgets until the toolbar builds it");
    bar.hover = true;
    check(bar.chips(true, false).affordancesPainted, "setting hover inks the pair");
    bar.editing = true;
    check(bar.chips(true, false).marks, "setting editing brings up the marks");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

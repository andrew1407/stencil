// Headless check of app/popoverHost.hpp — what a mouse press means while a compact
// popover is open. The widget lookups behind the facts (is the point inside the popover,
// does a nested dialog own it, did it land on the logo) need a live window, but the
// verdict they feed does not, and that verdict is the fiddly part: the logo's accent
// popover answers presses differently from every other popover, and a dismissal has to
// mark the click so the icon it lands on does not re-open what it just closed.
#include "popoverHost.hpp"

#include <QCoreApplication>

#include "support/check.hpp"

using stencil::gui::PopoverHost;
using Facts = PopoverHost::PressFacts;

// A press on empty window chrome while an ordinary popover is open.
static Facts outside() { return Facts{}; }

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ── The default: a press anywhere else closes the popover and travels on.
  {
    const auto v = PopoverHost::judgePress(outside(), Qt::LeftButton);
    check(v.dismiss, "a press outside dismisses the popover");
    check(!v.consume, "…and still reaches whatever it landed on");
    check(!v.armDismissClick, "empty chrome cannot re-open anything, so nothing is armed");
  }

  // ── Inside the popover, or inside a dialog it opened, is not "outside".
  {
    Facts f = outside();
    f.insidePopover = true;
    const auto v = PopoverHost::judgePress(f, Qt::LeftButton);
    check(!v.dismiss && !v.consume, "a press inside the popover is left entirely alone");
  }
  {
    Facts f = outside();
    f.otherWindowOwns = true;
    const auto v = PopoverHost::judgePress(f, Qt::LeftButton);
    check(!v.dismiss && !v.consume, "a nested dialog or menu owns its own presses");
  }
  {
    // A nested dialog drawn over the popover: ownership wins, so a flow launched from
    // inside the popover is never cut off by its own confirm box.
    Facts f = outside();
    f.insidePopover = true;
    f.otherWindowOwns = true;
    check(!PopoverHost::judgePress(f, Qt::LeftButton).dismiss, "both exemptions agree");
  }

  // ── The click that dismissed must not immediately re-open through the icon under it.
  {
    Facts f = outside();
    f.onPopoverIcon = true;
    const auto v = PopoverHost::judgePress(f, Qt::LeftButton);
    check(v.dismiss && v.armDismissClick,
          "a press on a popover icon dismisses and arms the swallow");
    check(!v.consume, "the press still travels — the arming is what stops the re-open");
  }

  // ── The logo's accent popover, the one exception (browser parity).
  {
    Facts f = outside();
    f.onLogo = true;
    f.accentPopover = true;
    f.peekingAccent = true;
    const auto right = PopoverHost::judgePress(f, Qt::RightButton);
    check(!right.dismiss && right.consume && right.clearPeek,
          "right-pressing the logo promotes the peek to sticky instead of closing it");
    const auto left = PopoverHost::judgePress(f, Qt::LeftButton);
    check(!left.dismiss && left.consume && !left.clearPeek,
          "left-pressing mid-peek is a no-op, swallowed so nothing cycles");
  }
  {
    // The same popover, now sticky: a left press falls through to the logo, whose click
    // cycles the accent under the still-open list.
    Facts f = outside();
    f.onLogo = true;
    f.accentPopover = true;
    f.peekingAccent = false;
    const auto v = PopoverHost::judgePress(f, Qt::LeftButton);
    check(!v.dismiss && !v.consume, "a left press on a sticky accent popover reaches the logo");
    check(!v.armDismissClick, "and nothing is armed, because nothing was dismissed");
    const auto right = PopoverHost::judgePress(f, Qt::RightButton);
    check(right.consume && right.clearPeek, "a right press stays sticky either way");
  }
  {
    // Any OTHER button on the logo is not one of the two gestures: it dismisses.
    Facts f = outside();
    f.onLogo = true;
    f.accentPopover = true;
    f.onPopoverIcon = true;
    const auto v = PopoverHost::judgePress(f, Qt::MiddleButton);
    check(v.dismiss && v.armDismissClick, "a middle press is an ordinary dismissal");
  }
  {
    // The logo while some OTHER popover is open gets no exemption at all.
    Facts f = outside();
    f.onLogo = true;
    f.accentPopover = false;
    f.onPopoverIcon = true;
    const auto v = PopoverHost::judgePress(f, Qt::RightButton);
    check(v.dismiss && !v.consume, "the exemption belongs to the accent popover, not the logo");
  }
  {
    // …and a press inside the accent popover itself still wins over the logo rules.
    Facts f = outside();
    f.insidePopover = true;
    f.onLogo = true;
    f.accentPopover = true;
    f.peekingAccent = true;
    const auto v = PopoverHost::judgePress(f, Qt::RightButton);
    check(!v.consume && !v.clearPeek, "inside the list, the logo gestures do not apply");
  }

  // ── The host's own state starts empty: no popover, no peek, nothing armed.
  {
    PopoverHost h;
    check(!h.active && !h.overlay && !h.anchor, "a fresh host has no popover on screen");
    check(!h.peekAction && !h.peekNextAction && !h.peekExportMenu, "and no peek in flight");
    check(!h.swallowRelease && !h.dismissClick, "and no swallow armed");
    check(h.dialogActions.isEmpty() && h.buttons.isEmpty(), "and no icons registered yet");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

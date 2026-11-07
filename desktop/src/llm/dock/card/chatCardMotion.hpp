#pragma once
// A cleared card's exit without particles, the reverse of its entrance (browser chat/leave.js chatLeave and
// css motionModes.css): Sliding pours it back into its tail home, Transition shrinks it into its tail corner.
// The clear's wait for the returning chips rides the same clocks.

#include "../../../support/tune/motionTable.hpp"

class QScrollArea;
class QWidget;

namespace stencil::gui {

  // The shrink into the tail corner, ms (common/config/motion.json CHAT_SHRINK_MS): browser chatGrowIn reversed.
  inline int chatShrinkMs() { return support::motionTableMs("CHAT_SHRINK_MS", 340); }

  // Plays the exit over the transcript's view and hides `card` at once; false (nothing done) in other modes.
  bool chatCardLeave(QWidget* card, QScrollArea* scroll);

  // How long a cleared transcript waits before the chips return: the leaving cards' flight, a hair past the dust's.
  int chatClearWaitMs();

}  // namespace stencil::gui

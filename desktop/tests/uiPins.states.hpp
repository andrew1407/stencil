// The rendered half of the appearance pins: the twelve MainWindow / dialog states.
// main() and the stylesheet hash live in uiPins.headless.cpp, the grab/compare
// machinery in support/uiPin.hpp. `png` is a small image the states load.
#pragma once

#include <QString>

void pinWindowStates(const QString& png);
void pinDialogs(const QString& png);

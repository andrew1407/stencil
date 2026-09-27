#pragma once
// The core the canvas is built on, gathered at the seam: the document types (lines, the crop, the
// undo steps), the crop and turn math, the filter rows, the chain edits and the hold-to-draw
// controller. canvas/ reaches core through here and not by its own includes.
#include "cropGeometry.hpp"
#include "cropSnap.hpp"
#include "HistoryStack.hpp"
#include "holdDraw.hpp"
#include "imageFilter.hpp"
#include "lineChain.hpp"
#include "models.hpp"

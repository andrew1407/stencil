#pragma once
// The motion-prefs suite's sections, one TU each behind this header, called in this order
// from main().
#include "fileStore.hpp"
#include "DisintegrateOverlay.hpp"
#include "motionIcons.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <cmath>
#include <cstdio>

using namespace stencil::gui;
namespace support = stencil::support;
using support::MotionMode;

#include "../support/check.hpp"

namespace motionprefs {

  void checkModeStorageAndStyles();
  void checkGateAndGlyphs();

}  // namespace motionprefs

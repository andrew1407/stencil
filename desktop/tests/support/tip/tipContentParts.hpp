#pragma once
// Shared ground for the tooltip-content headless TUs: the names the cases use and the section
// each .cpp contributes. check.hpp's `failures` is inline, so the TUs share one count.
#include "tipContent.hpp"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QKeySequence>
#include <QToolTip>
#include <QWidget>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <cstdio>

using stencil::gui::isKeyCombo;
using stencil::gui::parseTip;
using stencil::gui::renderTip;
using stencil::gui::themePalette;
using stencil::gui::Tip;
using stencil::gui::TipBlock;

#include "../../support/check.hpp"
using stencil::gui::Palette;

void parseCases();
void renderCases(const Palette& pal);
void composeCases(const Palette& pal);

#pragma once
// Shared ground for the MainWindow paint GUI suites: comparing two painted colours within a
// tolerance, since a QSS-styled control is anti-aliased and never an exact RGB match.
#include "MainWindow.gui.hpp"

namespace stencil::guitest {

  inline bool nearColor(const QColor& a, const QColor& b, int tol) {
    return qAbs(a.red() - b.red()) < tol && qAbs(a.green() - b.green()) < tol
           && qAbs(a.blue() - b.blue()) < tol;
  }

}  // namespace stencil::guitest

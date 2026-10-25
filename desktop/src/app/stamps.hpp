#pragma once

// The wall-clock stamp and the random salt a record, a toast or a co-edit message carries.

#include <QDateTime>
#include <QRandomGenerator>
#include <QString>
#include <string>

namespace stencil::gui {

  inline long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

  inline std::string makeSalt() {
    return QString::number(QRandomGenerator::global()->bounded(1 << 24), 36)
        .toStdString();
  }

}  // namespace stencil::gui

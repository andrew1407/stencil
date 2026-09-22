#pragma once
// Which widget is taking the keys (browser utils.js isTypingTarget): a text box, a spin box or a
// combo — the container, which is its line edit's focus proxy and what focusWidget() reports.
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace stencil::support {

  inline bool isTextEntry(QObject* o) {
    return qobject_cast<QLineEdit*>(o) || qobject_cast<QPlainTextEdit*>(o) ||
           qobject_cast<QTextEdit*>(o) || qobject_cast<QAbstractSpinBox*>(o) ||
           qobject_cast<QComboBox*>(o);
  }

}  // namespace stencil::support

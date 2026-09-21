#pragma once

#include <QFile>
#include <QIODevice>
#include <QString>

// The .stc file half both script surfaces share: the window's Upload/Download and the
// flyout's, which the window performs on its behalf (a file dialog cannot open under a grab).
namespace stencil::gui {

  inline const QString& scriptFileFilter() {
    static const QString filter = QStringLiteral("Stencil script (*.stc)");
    return filter;
  }

  inline bool readScriptFile(const QString& path, QString* text) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    if (text) *text = QString::fromUtf8(file.readAll());
    return true;
  }

  inline bool writeScriptFile(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    return file.write(text.toUtf8()) >= 0;
  }

}  // namespace stencil::gui

#pragma once
// Local-path helpers shared by the assistant's filesystem ops and any user-typed path.
// cli twins: pipeline.zig (expandHome), llm.zig (understoodPath).
#include <QDir>
#include <QString>

namespace stencil::support {

  // A chat/console path never passes a shell, so `~/Downloads/x.png` would otherwise
  // mean a folder actually named "~".
  inline QString expandHomePath(const QString& path) {
    if (path == QLatin1String("~")) return QDir::homePath();
    if (path.startsWith(QLatin1String("~/"))) return QDir::homePath() + path.mid(1);
    return path;
  }

  // "" when the path has none.
  inline QString fileExtensionOf(const QString& path) {
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return QString();
    const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
    if (dot < slash) return QString();  // the dot is in a directory name
    return path.mid(dot + 1).toLower();
  }

}  // namespace stencil::support

#pragma once
// Local-path helpers shared by the assistant's filesystem ops (contract §10 openFile / the
// save destination) and anything else handed a path a user typed. Header-only, Qt-only —
// the cli twins live in pipeline.zig (expandHome) and llm.zig (understoodPath).
#include <QDir>
#include <QString>

namespace stencil::support {

  // Expand a leading `~` (bare, or `~/…`) to the home directory. A path typed into a chat or
  // a console never passes through a shell, so it reaches us literally and `~/Downloads/x.png`
  // would otherwise mean a folder actually named "~".
  inline QString expandHomePath(const QString& path) {
    if (path == QLatin1String("~")) return QDir::homePath();
    if (path.startsWith(QLatin1String("~/"))) return QDir::homePath() + path.mid(1);
    return path;
  }

  // The extension of a path's FILE component, lowercased ("" when it has none — i.e. the path
  // names a folder as far as we can tell).
  inline QString fileExtensionOf(const QString& path) {
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return QString();
    const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
    if (dot < slash) return QString();  // the dot is in a directory name
    return path.mid(dot + 1).toLower();
  }

}  // namespace stencil::support

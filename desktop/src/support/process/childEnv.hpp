#pragma once
// The environment a child process starts with: the parent's, without the credentials — every
// STENCIL_LLM_* key and the server tokens. Twin of cli/src/safety/child.zig's scrubbedEnv.
#include <QProcessEnvironment>
#include <QStringList>

namespace stencil::support {

  // Case-insensitive, as Windows folds environment keys.
  inline QProcessEnvironment scrubbedChildEnv(
      QProcessEnvironment env = QProcessEnvironment::systemEnvironment()) {
    for (const QString& key : env.keys())
      if (key.startsWith(QLatin1String("STENCIL_LLM_"), Qt::CaseInsensitive) ||
          key.compare(QLatin1String("STENCIL_SERVER_TOKEN"), Qt::CaseInsensitive) == 0 ||
          key.compare(QLatin1String("STENCIL_SERVER_TOKENS"), Qt::CaseInsensitive) == 0)
        env.remove(key);
    return env;
  }

}  // namespace stencil::support

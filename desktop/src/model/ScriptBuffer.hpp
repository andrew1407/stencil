#pragma once

#include <QObject>
#include <QString>

namespace stencil::model {

  /* The ONE .stc both desktop hosts edit — the script window and the context menu's flyout.
   * Session-scoped: it outlives either host closing and is never written to settings, a
   * project or a file, because a pasted script is untrusted text. */
  class ScriptBuffer : public QObject {
    Q_OBJECT

   public:
    static ScriptBuffer& instance();

    const QString& text() const { return text_; }
    // No-op when the text is unchanged, so a host echoing its own write starts no loop.
    void setText(const QString& text);

   signals:
    void changed(const QString& text);

   private:
    QString text_;
  };

}  // namespace stencil::model

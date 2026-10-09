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

    const QString& getText() const { return text; }
    // No-op when the text is unchanged, so a host echoing its own write starts no loop and keeps
    // the origin; any other text replaces it, a link's or the user's.
    void setText(const QString& text, bool fromLink = false);
    // The text is still the script a stencil:// link delivered: it may read web images only.
    bool isFromLink() const { return fromLink; }

   signals:
    void changed(const QString& text);

   private:
    QString text;
    bool fromLink = false;
  };

}  // namespace stencil::model

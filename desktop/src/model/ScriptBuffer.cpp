#include "ScriptBuffer.hpp"

namespace stencil::model {

  ScriptBuffer& ScriptBuffer::instance() {
    static ScriptBuffer buffer;
    return buffer;
  }

  void ScriptBuffer::setText(const QString& text, bool fromLink) {
    if (this->text == text) return;
    this->text = text;
    this->fromLink = fromLink;
    emit changed(this->text);
  }

}  // namespace stencil::model

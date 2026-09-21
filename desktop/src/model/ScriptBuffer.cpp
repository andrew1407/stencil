#include "ScriptBuffer.hpp"

namespace stencil::model {

  ScriptBuffer& ScriptBuffer::instance() {
    static ScriptBuffer buffer;
    return buffer;
  }

  void ScriptBuffer::setText(const QString& text) {
    if (this->text == text) return;
    this->text = text;
    emit changed(this->text);
  }

}  // namespace stencil::model

#include "ScriptBuffer.hpp"

namespace stencil::model {

  ScriptBuffer& ScriptBuffer::instance() {
    static ScriptBuffer buffer;
    return buffer;
  }

  void ScriptBuffer::setText(const QString& text) {
    if (text_ == text) return;
    text_ = text;
    emit changed(text_);
  }

}  // namespace stencil::model

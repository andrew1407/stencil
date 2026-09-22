#pragma once
// The letter a key press spells for the typed words (browser ui/bindings/keys/typedWords.js
// typedLetter): the layout's own letter when it is Latin, else the letter the same physical
// key carries on the US layout, so a word typed under a Cyrillic layout still lands.
#include <QChar>
#include <QtGlobal>

class QKeyEvent;

namespace stencil::support {

  enum class KeyPlatform { MAC, WINDOWS, LINUX };
  KeyPlatform hostKeyPlatform();

  // The US-layout letter of a physical letter key, or null: macOS reads the virtual key
  // (kVK_ANSI_*), Windows its VK code, Linux the evdev scan code as X11/xkb number it.
  QChar latinLetterOfNative(KeyPlatform platform, quint32 virtualKey, quint32 scanCode);
  // The inverse, for a test that fabricates a key press (kVK_ANSI_A is 0, so 0 is no sentinel).
  quint32 nativeCodeOfLetter(KeyPlatform platform, QChar letter);

  QChar typedLetter(const QKeyEvent& e);

}  // namespace stencil::support

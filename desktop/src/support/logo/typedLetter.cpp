#include "typedLetter.hpp"

#include <QKeyEvent>
#include <QString>
#include <cstddef>

namespace stencil::support {

  namespace {
    constexpr KeyPlatform HOST =
#if defined(Q_OS_MACOS)
        KeyPlatform::MAC;
#elif defined(Q_OS_WIN)
        KeyPlatform::WINDOWS;
#else
        KeyPlatform::LINUX;
#endif

    struct NativeLetter {
      quint32 code;
      char letter;
    };

    // kVK_ANSI_* (Carbon Events.h): the ANSI layout's positions, whatever layout is active.
    constexpr NativeLetter MAC[] = {
        {0x00, 'a'}, {0x01, 's'}, {0x02, 'd'}, {0x03, 'f'}, {0x04, 'h'}, {0x05, 'g'}, {0x06, 'z'},
        {0x07, 'x'}, {0x08, 'c'}, {0x09, 'v'}, {0x0B, 'b'}, {0x0C, 'q'}, {0x0D, 'w'}, {0x0E, 'e'},
        {0x0F, 'r'}, {0x10, 'y'}, {0x11, 't'}, {0x1F, 'o'}, {0x20, 'u'}, {0x22, 'i'}, {0x23, 'p'},
        {0x25, 'l'}, {0x26, 'j'}, {0x28, 'k'}, {0x2D, 'n'}, {0x2E, 'm'}};

    // evdev KEY_* + 8: the X11 keycode, and what xkbcommon hands Qt on Wayland too.
    constexpr NativeLetter LINUX[] = {
        {24, 'q'}, {25, 'w'}, {26, 'e'}, {27, 'r'}, {28, 't'}, {29, 'y'}, {30, 'u'}, {31, 'i'},
        {32, 'o'}, {33, 'p'}, {38, 'a'}, {39, 's'}, {40, 'd'}, {41, 'f'}, {42, 'g'}, {43, 'h'},
        {44, 'j'}, {45, 'k'}, {46, 'l'}, {52, 'z'}, {53, 'x'}, {54, 'c'}, {55, 'v'}, {56, 'b'},
        {57, 'n'}, {58, 'm'}};

    bool isLatin(QChar c) { return c >= QLatin1Char('a') && c <= QLatin1Char('z'); }

    template <size_t N>
    QChar lookUp(const NativeLetter (&table)[N], quint32 code) {
      for (const NativeLetter& row : table)
        if (row.code == code) return QLatin1Char(row.letter);
      return QChar();
    }

    template <size_t N>
    quint32 lookUpCode(const NativeLetter (&table)[N], char letter) {
      for (const NativeLetter& row : table)
        if (row.letter == letter) return row.code;
      return 0;
    }
  }  // namespace

  KeyPlatform hostKeyPlatform() { return HOST; }

  QChar latinLetterOfNative(KeyPlatform platform, quint32 virtualKey, quint32 scanCode) {
    switch (platform) {
      case KeyPlatform::MAC: return lookUp(MAC, virtualKey);
      case KeyPlatform::LINUX: return lookUp(LINUX, scanCode);
      case KeyPlatform::WINDOWS:   // VK_A..VK_Z are the letters themselves
        return virtualKey >= 'A' && virtualKey <= 'Z' ? QChar(virtualKey).toLower() : QChar();
    }
    return QChar();
  }

  quint32 nativeCodeOfLetter(KeyPlatform platform, QChar letter) {
    const QChar low = letter.toLower();
    if (!isLatin(low)) return 0;
    switch (platform) {
      case KeyPlatform::MAC: return lookUpCode(MAC, low.toLatin1());
      case KeyPlatform::LINUX: return lookUpCode(LINUX, low.toLatin1());
      case KeyPlatform::WINDOWS: return quint32(low.toUpper().unicode());
    }
    return 0;
  }

  // Only a letter of another alphabet reads the physical key: a fabricated press carries native
  // code 0, which on macOS IS the A key. Any other printable character joins the buffer as typed.
  QChar typedLetter(const QKeyEvent& e) {
    const QString text = e.text();
    const QChar typed = text.size() == 1 ? text.at(0).toLower() : QChar();
    if (isLatin(typed) || !typed.isLetter()) return typed.isPrint() ? typed : QChar();
    const QChar physical = latinLetterOfNative(HOST, e.nativeVirtualKey(), e.nativeScanCode());
    return physical.isNull() ? typed : physical;
  }

}  // namespace stencil::support

#pragma once
// The chat-card vocabulary the dock and the compact composer both draw with: a bubble's colours and
// side, its "…" row menu, the widths a wrapped label needs and the sheet a swap re-styles it with.
// Browser twin: browser/js/ui/chatView.js. Included from ChatDock.hpp, so callers keep their spelling.
#include <QColor>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../../support/theme/theme.hpp"   // Palette
#include <functional>

class QFrame;
class QLabel;
class QLayout;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QToolButton;
class QWidget;

namespace stencil::gui {


  // `onPick` gets the prompt text — callers prefill the composer, never send.
  QWidget* makeSuggestionChips(QWidget* parent, int gap, std::function<void(QString)> onPick);
  void styleSuggestionChips(QWidget* chips, const Palette& pal);

  extern const char* const CHAT_STATUS_OK_COLOR;
  extern const char* const CHAT_STATUS_BAD_COLOR;

  QToolButton* makeChatAccentButton(QWidget* parent, const QString& tooltip);

  enum class ChatCardKind { BUBBLE, ERROR, MUTED };
  QLabel* fillChatCard(QFrame* card, QVBoxLayout* lay, const QString& role,
                       const QString& text, ChatCardKind kind, const QColor& danger);
  // Pure: `swapped` flips the side, `user` alone decides it at rest.
  inline bool isChatBubbleOnRight(bool user, bool swapped) { return swapped ? !user : user; }
  // OPAQUE, flattened over `pageBg` (the surface the cards sit ON, not `chip`); false ⇒ no tail.
  bool chatBubbleColorsFor(const QString& objectName, const QColor& accent, const QColor& chip,
                           const QColor& border, const QColor& danger, const QColor& pageBg,
                           QColor& fillOut, QColor& borderOut);
  void applyChatBubbleSide(QFrame* card, QLayout* layout, bool right, const QColor& accent,
                           const QColor& chip, const QColor& border, const QColor& danger,
                           const QColor& pageBg);
  void applyChatSwapToCards(QWidget* transcript, QLayout* layout, bool swapped,
                            const QColor& accent, const QColor& chip, const QColor& border,
                            const QColor& danger, const QColor& pageBg);
  // Browser chatView.js chatRowMenuItems; only these hooks differ per surface.
  struct ChatCardMenuHooks {
    QWidget* owner = nullptr;
    QScrollArea* scroll = nullptr;
    std::function<void(const QString&)> insertIntoPrompt;
    std::function<void(QFrame*, const QString&)> resend;
    std::function<bool()> busy;
    // A dock mid-close is still visible for its slide; a menu opened then has nowhere to live.
    std::function<bool()> leaving;
    std::function<void()> moreMoved;
    // Global-coords furniture the "…" must not sit under; re-asked on every placement.
    std::function<QRect()> avoidRect;
    QColor text, chip, border, accent, muted;
  };
  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks);
  // The default arg lives on the one declaration in chatWidgets.hpp — repeating it is a redefinition.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal);
  QWidget* makeChatTypingDots(QWidget* parent);
  QToolButton* addChatRetryButton(QVBoxLayout* lay, const QColor& glyph,
                                  std::function<void()> onClick);
  QPushButton* addChatConfigureCta(QVBoxLayout* lay, const QColor& accent,
                                   std::function<void(QPushButton*)> onClick);
  // One bubble per turn, notes inside it, never extra rows.
  QLabel* addChatCardNote(QVBoxLayout* lay, const QString& text);

  // A word-wrapped QLabel clips its own last line unless its real height is reserved.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll);
  // Keyed on the whole sheet: a card's own local QSS shifts its wrapped label's height.
  QString chatCardStyleSheet(const Palette& pal, bool swapped);
}  // namespace stencil::gui

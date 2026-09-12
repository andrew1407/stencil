// Appending user, assistant and ask cards.
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/shimmerOverlay.hpp"
#include "chatWidgets.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>
#include <QStringList>

namespace stencil::gui {

  using namespace chatdock;
  void ChatDock::appendUser(const QString& text, const QList<QImage>& images) {
    QVBoxLayout* lay = appendCard("You", text, CardKind::Bubble);
    // Sending always lands the view at the very bottom, wherever it was.
    scrollToBottom();
    if (images.isEmpty()) return;
    // The attached images ARE part of what the user said, so they sit in the user's
    // own bubble as thumbnails — above the text, right-aligned with the bubble.
    // Previously only the count was appended as "[N image(s) attached]".
    QWidget* card = lay->parentWidget();
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addStretch(1);
    for (const QImage& img : images) {
      if (img.isNull()) continue;
      auto* thumb = new QLabel(card);
      thumb->setPixmap(QPixmap::fromImage(
          img.scaled(THUMB_EDGE, THUMB_EDGE, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      // No tooltip: the bubble thumbnail is already big, and "Attached image (500×750)"
      // told you nothing the picture doesn't. The hover preview declines to open for a
      // thumbnail this size, so hovering here does nothing at all — which is right.
      new HoverPreview(thumb, img, QString());
      row->addWidget(thumb);
    }
    // The text label is already in the layout — put the strip above it.
    lay->insertLayout(0, row);
    // The turn's images ride the card so the context menu's Resend can requeue
    // them; the thumbs joined after appendCard, so wire the menu onto them too.
    QVariantList stored;
    for (const QImage& img : images) stored << QVariant::fromValue(img);
    card->setProperty("chatImages", stored);
    installCardMenu(qobject_cast<QFrame*>(card));
    applyBubbleWidths();
  }

  void ChatDock::appendAssistant(const QString& text, const QStringList& warnings,
                                 const QStringList& notes) {
    QString t = text;
    for (const QString& w : warnings) t += QStringLiteral("\n⚠ ") + w;
    QVBoxLayout* lay = appendCard("Assistant", t, CardKind::Bubble);
    lastAssistantCard_ = lay->parentWidget();   // late notes merge into THIS bubble
    // Executor notes ride WITH the reply — muted lines in the SAME bubble, never
    // a separate card (browser parity: they merge into the reply's warnings).
    for (const QString& n : notes) {
      auto* note = makePlainLabel(n, lay->parentWidget());
      note->setObjectName(QStringLiteral("chatNoteLabel"));  // muted via the QSS rule
      note->setWordWrap(true);
      note->setProperty("chatNote", n);
      applyMutedText(note);  // fallback tone when no stylesheet is active
      lay->addWidget(note);
    }
    if (!notes.isEmpty()) {
      installCardMenu(qobject_cast<QFrame*>(lay->parentWidget()));  // wire the fresh notes
      applyBubbleWidths();  // re-measure with the extra labels
    }
  }

  void ChatDock::appendAsk(const stencil::llm::AskCard& ask, const QVector<QImage>& previews) {
    if (ask.options.isEmpty()) return;
    QVBoxLayout* lay = appendTranscriptCard(6);
    QWidget* card = lay->parentWidget();
    lay->addWidget(makeRoleLabel(QStringLiteral("Assistant asks"), card));

    auto* question = makePlainLabel(ask.question, card);
    question->setWordWrap(true);
    lay->addWidget(question);

    // One group per card so a single-pick card's radios are exclusive to it — several cards
    // can sit in the transcript at once, and Qt would otherwise link every radio in the dock.
    auto* group = new QButtonGroup(card);
    group->setExclusive(!ask.multi);
    QVector<QAbstractButton*> buttons;
    for (int i = 0; i < ask.options.size(); ++i) {
      auto* row = new QWidget(card);
      auto* rowLay = new QHBoxLayout(row);
      rowLay->setContentsMargins(0, 0, 0, 0);
      rowLay->setSpacing(6);
      QAbstractButton* pick = ask.multi ? static_cast<QAbstractButton*>(new QCheckBox(row))
                                        : static_cast<QAbstractButton*>(new QRadioButton(row));
      group->addButton(pick, i);
      buttons.push_back(pick);
      rowLay->addWidget(pick, 0);
      if (i < previews.size() && !previews[i].isNull()) {
        auto* thumb = new QLabel(row);
        thumb->setPixmap(QPixmap::fromImage(
            previews[i].scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        rowLay->addWidget(thumb, 0);
      }
      auto* label = makePlainLabel(ask.options[i].label, row);
      label->setWordWrap(true);
      rowLay->addWidget(label, 1);
      lay->addWidget(row);
    }

    // The custom row: ticking it or typing in it means the same thing, so they stay in step.
    QAbstractButton* customPick = nullptr;
    QLineEdit* customText = nullptr;
    if (ask.allowCustom) {
      auto* row = new QWidget(card);
      auto* rowLay = new QHBoxLayout(row);
      rowLay->setContentsMargins(0, 0, 0, 0);
      rowLay->setSpacing(6);
      customPick = ask.multi ? static_cast<QAbstractButton*>(new QCheckBox(row))
                             : static_cast<QAbstractButton*>(new QRadioButton(row));
      group->addButton(customPick, ask.options.size());
      rowLay->addWidget(customPick, 0);
      customText = new QLineEdit(row);
      customText->setPlaceholderText(ask.customLabel);
      rowLay->addWidget(customText, 1);
      lay->addWidget(row);
    }

    auto* submit = new QPushButton(QStringLiteral("Submit"), card);
    // The affirmative action of the card, so it wears the app's accent CTA face
    // (theme.cpp QPushButton[accentCta="true"]) and the same hover sweep every other
    // button in the app carries — the browser's twin is a plain <button>, which gets both
    // for free from its shared rules (css/layout.css, .chat-ask-submit).
    submit->setProperty("accentCta", true);
    installHoverShimmer(submit);
    submit->setEnabled(false);
    lay->addWidget(submit, 0, Qt::AlignLeft);

    // Enabled only once something is chosen — a Submit that sends nothing is a dead control.
    auto sync = [submit, buttons, customPick, customText]() {
      bool any = false;
      for (QAbstractButton* b : buttons) any = any || b->isChecked();
      if (customPick && customPick->isChecked() && customText && !customText->text().trimmed().isEmpty()) any = true;
      submit->setEnabled(any);
    };
    for (QAbstractButton* b : buttons) connect(b, &QAbstractButton::toggled, card, sync);
    if (customPick) connect(customPick, &QAbstractButton::toggled, card, sync);
    if (customText) {
      connect(customText, &QLineEdit::textChanged, card, [customPick, sync]() {
        if (customPick) customPick->setChecked(true);
        sync();
      });
    }

    connect(submit, &QPushButton::clicked, card,
            [this, card, submit, buttons, customPick, customText, opts = ask.options]() {
              QStringList picked;
              for (int i = 0; i < buttons.size(); ++i) {
                if (buttons[i]->isChecked()) picked << opts[i].label;
              }
              const QString custom =
                  (customPick && customPick->isChecked() && customText) ? customText->text() : QString();
              const QString answer = stencil::llm::askAnswerText(picked, custom);
              if (answer.isEmpty()) return;
              // Lock it: the card becomes the record of what was sent, not a control.
              for (QAbstractButton* b : buttons) b->setEnabled(false);
              if (customPick) customPick->setEnabled(false);
              if (customText) customText->setEnabled(false);
              submit->setVisible(false);
              auto* sent = makePlainLabel(answer, card);   // may be the user's free text
              sent->setWordWrap(true);
              applyMutedText(sent);
              if (auto* lay = qobject_cast<QVBoxLayout*>(card->layout())) lay->addWidget(sent);
              emit sendRequested(answer);
            });
  }
}  // namespace stencil::gui

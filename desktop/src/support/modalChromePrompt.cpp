#include "modalChrome.hpp"
#include "modalChromeShared.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"
#include "shimmerOverlay.hpp"

#include <QColor>
#include <QComboBox>
#include <QGuiApplication>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

namespace stencil::gui {

  std::optional<QString> promptModal(QWidget* parent, const PromptSpec& spec) {
    QDialog dlg(parent);
    dlg.setObjectName(QStringLiteral("stencilPromptModal"));
    dlg.setWindowTitle(spec.title);
    ModalChrome chrome = installModalChrome(&dlg, spec.titleIcon, spec.title);
    auto* caption = new QLabel(spec.message, &dlg);
    caption->setWordWrap(true);
    chrome.body->addWidget(caption);

    QLineEdit* line = nullptr;
    QPlainTextEdit* area = nullptr;
    if (spec.multiline) {
      area = new QPlainTextEdit(spec.defaultValue, &dlg);
      area->setObjectName(QStringLiteral("modalPromptText"));
      area->setTabChangesFocus(true);   // Tab leaves the field; it never types a tab here
      // The field's OWN metrics: a fixed pixel height would drift with the platform font.
      const int pad = 16;
      area->setFixedHeight(area->fontMetrics().lineSpacing() * qMax(1, spec.rows) + pad);
      area->selectAll();
      chrome.body->addWidget(area);
    } else {
      line = new QLineEdit(spec.defaultValue, &dlg);
      line->setObjectName(QStringLiteral("modalPromptLine"));
      if (spec.password) line->setEchoMode(QLineEdit::Password);
      line->selectAll();
      chrome.body->addWidget(line);
    }
    auto* reason = new QLabel(&dlg);
    reason->setObjectName(QStringLiteral("modalPromptReason"));
    reason->setWordWrap(true);
    reason->hide();
    chrome.body->addWidget(reason);
    chrome.body->addStretch(1);

    QHBoxLayout* footer = addModalFooter(chrome);
    auto* cancelBtn = new QPushButton(spec.cancelLabel, &dlg);
    makeModalCta(cancelBtn, QStringLiteral("x"));
    footer->addWidget(cancelBtn);
    auto* okBtn = new QPushButton(spec.confirmLabel, &dlg);
    makeModalCta(okBtn, spec.confirmIcon);
    footer->addWidget(okBtn);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    // The cursor follows, Qt having no `:disabled { cursor }` in QSS.
    const auto revalidate = [&spec, line, area, okBtn, reason] {
      if (!spec.validate) return;
      const QString text = (area ? area->toPlainText() : line->text()).trimmed();
      const QString why = spec.validate(text);
      const bool ok = why.isEmpty();
      okBtn->setEnabled(ok);
      okBtn->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
      okBtn->setToolTip(why);
      reason->setText(why);
      reason->setVisible(!ok);
    };
    if (line) QObject::connect(line, &QLineEdit::textChanged, &dlg, revalidate);
    else QObject::connect(area, &QPlainTextEdit::textChanged, &dlg, revalidate);
    revalidate();
    // The text AREA owns plain Enter, so only Ctrl/⌘+Enter saves from inside it; the
    // buttons stay out of Qt's default-button chain.
    if (line) {
      okBtn->setDefault(true);
      okBtn->setAutoDefault(true);
      QObject::connect(line, &QLineEdit::returnPressed, &dlg, [&dlg, okBtn] {
        if (okBtn->isEnabled()) dlg.accept();
      });
    } else {
      cancelBtn->setAutoDefault(false);
      okBtn->setAutoDefault(false);
      auto* save = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), &dlg);
      QObject::connect(save, &QShortcut::activated, &dlg, &QDialog::accept);
      auto* saveEnter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), &dlg);
      QObject::connect(saveEnter, &QShortcut::activated, &dlg, &QDialog::accept);
    }
    dlg.setFixedWidth(kModalWidth);
    dlg.adjustSize();
    if (area) area->setFocus();
    else line->setFocus();
    armFlight(dlg, spec.flight);
    if (dlg.exec() != QDialog::Accepted) return std::nullopt;
    QString text = (area ? area->toPlainText() : line->text()).trimmed();
    if (spec.maxChars > 0) text = text.left(spec.maxChars);
    return text;
  }

  std::optional<QString> chooseModal(QWidget* parent, const ChooseSpec& spec) {
    QDialog dlg(parent);
    dlg.setObjectName(QStringLiteral("stencilChooseModal"));
    dlg.setWindowTitle(spec.title);
    ModalChrome chrome = installModalChrome(&dlg, spec.titleIcon, spec.title);
    auto* msg = new QLabel(spec.message, &dlg);
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::NoTextInteraction);
    chrome.body->addWidget(msg);
    // Browser .confirm-choose-row.
    auto* select = new QComboBox(&dlg);
    select->setObjectName(QStringLiteral("modalChooseSelect"));
    select->setCursor(Qt::PointingHandCursor);
    for (const ChooseOption& o : spec.options)
      select->addItem(o.label.isEmpty() ? o.value : o.label, o.value);
    if (spec.currentIndex >= 0 && spec.currentIndex < select->count())
      select->setCurrentIndex(spec.currentIndex);
    chrome.body->addSpacing(2);   // + the body's own 10px gap = the browser's 12px
    chrome.body->addWidget(select);
    chrome.body->addStretch(1);

    QHBoxLayout* footer = addModalFooter(chrome);
    auto* cancelBtn = new QPushButton(spec.cancelLabel, &dlg);
    makeModalCta(cancelBtn, QStringLiteral("x"));
    cancelBtn->setAutoDefault(false);
    footer->addWidget(cancelBtn);
    auto* okBtn = new QPushButton(spec.confirmLabel, &dlg);
    makeModalCta(okBtn, spec.confirmIcon);
    okBtn->setDefault(true);   // Enter confirms, Escape rejects (QDialog)
    okBtn->setAutoDefault(true);
    footer->addWidget(okBtn);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.setFixedWidth(kModalWidth);
    dlg.adjustSize();
    select->setFocus();   // the browser focuses its select
    armFlight(dlg, spec.flight);
    if (dlg.exec() != QDialog::Accepted || select->count() == 0) return std::nullopt;
    return select->currentData().toString();
  }
}  // namespace stencil::gui


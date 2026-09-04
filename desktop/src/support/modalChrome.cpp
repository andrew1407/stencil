#include "modalChrome.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"
#include "shimmerOverlay.hpp"

#include <QColor>
#include <QDialog>
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
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Browser .settings-header / .settings-body / .settings-footer padding
    // (components.css: 14px 18px / 14px 18px / 12px 18px).
    constexpr int kPadX = 18;
    constexpr int kHeaderPadY = 12;
    constexpr int kBodyPadY = 14;
    constexpr int kFooterPadY = 12;
    // The width the footer hint is guaranteed: the DIALOG's own minimum is raised to
    // reserve it (see addModalFooter), rather than the label carrying a hard floor of its
    // own — browser twin: the hint's `flex: 1 1 150px` basis.
    constexpr int kFooterHintMinW = 150;
  }  // namespace

  QFrame* modalDivider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("modalDivider"));
    line->setFrameShape(QFrame::NoFrame);   // the QSS paints it; a Sunken bevel doubles up
    line->setFixedHeight(1);
    return line;
  }

  QLabel* modalSectionLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text.toUpper(), parent);
    l->setObjectName(QStringLiteral("modalSection"));
    return l;
  }

  void alignModalForm(QFormLayout* form, bool growFields) {
    if (!form) return;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (growFields) form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  }

  void makeModalCta(QPushButton* btn, const QString& iconName) {
    if (!btn) return;
    // A dynamic property, not an objectName: theme.cpp matches
    // QPushButton[accentCta="true"], and the objectName stays free for tests.
    btn->setProperty("accentCta", true);
    // labelIcon, not themedIcon: the glyph carries the browser's 6px gap to its label.
    if (!iconName.isEmpty()) btn->setIcon(labelIcon(iconName, QColor("#ffffff"), 15));
  }

  namespace {
    // Frameless dialogs lose the OS title bar (browser parity: no traffic lights on a
    // modal), so the HEADER is the drag handle — press anywhere on it that is not a
    // button and the whole dialog moves with the cursor.
    class HeaderDrag : public QObject {
     public:
      HeaderDrag(QWidget* header, QDialog* dlg) : QObject(header), dlg_(dlg) {
        header->installEventFilter(this);
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        auto* me = static_cast<QMouseEvent*>(e);
        switch (e->type()) {
          case QEvent::MouseButtonPress:
            if (dlg_ && me->button() == Qt::LeftButton) {
              grab_ = me->globalPosition().toPoint() - dlg_->frameGeometry().topLeft();
              on_ = true;
            }
            break;
          case QEvent::MouseMove:
            if (on_ && dlg_) dlg_->move(me->globalPosition().toPoint() - grab_);
            break;
          case QEvent::MouseButtonRelease:
            on_ = false;
            break;
          default:
            break;
        }
        return QObject::eventFilter(o, e);
      }

     private:
      QPointer<QDialog> dlg_;
      QPoint grab_;
      bool on_ = false;
    };
  }  // namespace

  ModalChrome installModalChrome(QDialog* dlg, const QString& iconName, const QString& title) {
    ModalChrome c;
    // No OS title bar (browser parity: the modal's own header is the only chrome) and a
    // translucent window so the shell's rounded corners actually clip. The QSS half —
    // the transparent dialog face + the #modalShell card — lives in theme.cpp.
    dlg->setProperty("modalChrome", true);
    dlg->setWindowFlag(Qt::FramelessWindowHint, true);
    dlg->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* outer = new QVBoxLayout(dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* shell = new QWidget(dlg);
    shell->setObjectName(QStringLiteral("modalShell"));
    shell->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(shell);
    c.root = new QVBoxLayout(shell);
    // The dividers must run edge to edge (browser parity), so the root carries no
    // margins; the header/body/footer rows pad themselves instead. 1px inset keeps the
    // children off the shell's own border.
    c.root->setContentsMargins(1, 1, 1, 1);
    c.root->setSpacing(0);

    auto* headerW = new QWidget(shell);
    headerW->setObjectName(QStringLiteral("modalHeader"));
    headerW->setCursor(Qt::OpenHandCursor);
    new HeaderDrag(headerW, dlg);
    auto* header = new QHBoxLayout(headerW);
    header->setContentsMargins(kPadX, kHeaderPadY, kPadX, kHeaderPadY);
    header->setSpacing(8);
    if (!iconName.isEmpty()) {
      auto* glyph = new QLabel(dlg);
      const QColor txt = dlg->palette().color(QPalette::WindowText);
      glyph->setPixmap(themedIcon(iconName, txt, 18).pixmap(18, 18));
      header->addWidget(glyph);
    }
    auto* titleLbl = new QLabel(title, dlg);
    titleLbl->setObjectName(QStringLiteral("modalTitle"));
    header->addWidget(titleLbl);
    header->addStretch(1);
    c.close = new QPushButton(QObject::tr("Close"), dlg);
    c.close->setObjectName(QStringLiteral("modalClosePill"));
    c.close->setIcon(labelIcon("x", dlg->palette().color(QPalette::WindowText), 14));
    c.close->setCursor(Qt::PointingHandCursor);
    // Never the default button: Enter in a form must not dismiss the dialog.
    c.close->setAutoDefault(false);
    c.close->setDefault(false);
    QObject::connect(c.close, &QPushButton::clicked, dlg, &QDialog::reject);
    header->addWidget(c.close);
    c.root->addWidget(headerW);
    c.root->addWidget(modalDivider(dlg));

    c.body = new QVBoxLayout;
    c.body->setContentsMargins(kPadX, kBodyPadY, kPadX, kBodyPadY);
    c.body->setSpacing(10);
    c.root->addLayout(c.body, 1);
    // Every control in the window gets the app's glass hover sweep — the browser's rule
    // is app-wide, so a Qt window has to opt its own in. Deferred a turn, because the
    // caller fills the body after this returns.
    installHoverShimmerLater(dlg);
    return c;
  }

  namespace {
    // Claim the dialog's flight (support/modalReveal.hpp) so the app-wide watcher leaves it
    // alone, keeping its default origin — the press that raised it — but aiming the CLOSE
    // wherever the caller asked. Only worth claiming when there IS somewhere else to aim.
    void armFlight(QDialog& dlg, const FlightAnchors& flight) {
      if (!flight.openRect.isValid() && !flight.closeRect.isValid()) return;
      const QRect from = flight.openRect.isValid() ? flight.openRect
                                                   : support::gestureAnchorRect();
      support::revealDialog(dlg, nullptr, from, flight.closeRect);
    }
  }  // namespace

  ConfirmChoice confirmModalChoice(QWidget* parent, const ConfirmSpec& spec) {
    QDialog dlg(parent);
    dlg.setObjectName(QStringLiteral("stencilConfirmModal"));
    dlg.setWindowTitle(spec.title);
    ModalChrome chrome = installModalChrome(&dlg, QStringLiteral("alert"), spec.title);
    auto* msg = new QLabel(spec.message, &dlg);
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::NoTextInteraction);
    chrome.body->addWidget(msg);
    chrome.body->addStretch(1);
    QHBoxLayout* footer = addModalFooter(chrome);
    auto* cancelBtn = new QPushButton(spec.cancelLabel, &dlg);
    makeModalCta(cancelBtn, QStringLiteral("x"));
    footer->addWidget(cancelBtn);
    // The askAlt third button sits between Cancel and Confirm (browser order).
    bool altPicked = false;
    if (!spec.altLabel.isEmpty()) {
      auto* altBtn = new QPushButton(spec.altLabel, &dlg);
      makeModalCta(altBtn, spec.altIcon);
      footer->addWidget(altBtn);
      QObject::connect(altBtn, &QPushButton::clicked, &dlg, [&altPicked, &dlg] {
        altPicked = true;
        dlg.accept();
      });
    }
    auto* okBtn = new QPushButton(spec.confirmLabel, &dlg);
    if (spec.danger) {
      okBtn->setObjectName(QStringLiteral("dangerButton"));
      okBtn->setIcon(labelIcon(spec.confirmIcon, QColor("#ffffff"), 14));
    } else {
      makeModalCta(okBtn, spec.confirmIcon);
    }
    // Enter = Confirm (the browser focuses its confirm button); Escape rejects
    // via QDialog's own handling, same as the browser's key handler.
    okBtn->setDefault(true);
    okBtn->setAutoDefault(true);
    footer->addWidget(okBtn);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    // The browser confirm rides the shared .app-modal shell width, but sizes its
    // height to the question alone.
    dlg.setFixedWidth(kModalWidth);
    dlg.adjustSize();
    okBtn->setFocus();
    armFlight(dlg, spec.flight);
    if (dlg.exec() != QDialog::Accepted) return ConfirmChoice::Cancel;
    return altPicked ? ConfirmChoice::Alt : ConfirmChoice::Confirm;
  }

  bool confirmModal(QWidget* parent, const ConfirmSpec& spec) {
    return confirmModalChoice(parent, spec) == ConfirmChoice::Confirm;
  }

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
      // `rows` lines of the field's OWN metrics plus its frame/padding — a fixed pixel
      // height would drift with the platform font.
      const int pad = 16;
      area->setFixedHeight(area->fontMetrics().lineSpacing() * qMax(1, spec.rows) + pad);
      area->selectAll();
      chrome.body->addWidget(area);
    } else {
      line = new QLineEdit(spec.defaultValue, &dlg);
      line->setObjectName(QStringLiteral("modalPromptLine"));
      line->selectAll();
      chrome.body->addWidget(line);
    }
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
    // A single-line field confirms on Enter (browser parity). The text AREA owns plain
    // Enter — it types a newline — so only Ctrl/⌘+Enter saves from inside it, and the
    // buttons stay out of Qt's default-button chain so Enter never leaks to them.
    if (line) {
      okBtn->setDefault(true);
      okBtn->setAutoDefault(true);
      QObject::connect(line, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
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

  QHBoxLayout* addModalFooter(ModalChrome& chrome, const QString& hint) {
    QWidget* dlg = chrome.root ? chrome.root->parentWidget() : nullptr;
    chrome.root->addWidget(modalDivider(dlg));
    // Hint left, buttons right, on ONE row (browser .settings-footer). The hint takes all
    // the slack, so at a normal width it wraps at most a line or two instead of being
    // squeezed into a tall column of two-word lines (user report, with a picture).
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(kPadX, kFooterPadY, kPadX, kFooterPadY);
    footer->setSpacing(8);
    if (!hint.isEmpty()) {
      auto* h = new QLabel(hint, dlg);
      h->setObjectName(QStringLiteral("modalFooterHint"));
      h->setWordWrap(true);
      h->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      // …but that floor is a PREFERENCE, never a hard minimumWidth. A hard one Qt cannot
      // go under: once the row no longer fits, the layout hands it negative space and the
      // items overlap. Ignored lets the text yield the last pixels instead, wrapping
      // deeper, so nothing is ever drawn over.
      QSizePolicy sp(QSizePolicy::Ignored, QSizePolicy::Preferred);
      sp.setHeightForWidth(true);   // narrower ⇒ taller, so the wrap is never cut off
      h->setSizePolicy(sp);
      footer->addWidget(h, 1);
    } else {
      footer->addStretch(1);
    }
    chrome.root->addLayout(footer);
    // …and the WINDOW is what widens to hold the row. An explicit setMinimumSize (every
    // dialog sets one) stops SetDefaultConstraint from raising the minimum to what the
    // layout needs, so a wider system font runs the row out of space. Run once the caller
    // has added its buttons, and only ever upwards.
    if (dlg) {
      QTimer::singleShot(0, dlg, [dlg, footer] {
        QWidget* win = dlg->window();   // `dlg` here is the shell inside it
        if (!win) return;
        int need = footer->contentsMargins().left() + footer->contentsMargins().right() + 2;
        int items = 0;
        for (int i = 0; i < footer->count(); ++i) {
          QWidget* w = footer->itemAt(i)->widget();
          if (!w || w->isHidden()) continue;
          ++items;
          need += w->objectName() == QLatin1String("modalFooterHint")
                      ? kFooterHintMinW : w->minimumSizeHint().width();
        }
        need += footer->spacing() * qMax(0, items - 1);
        if (need > win->minimumWidth()) win->setMinimumWidth(need);
      });
    }
    return footer;
  }

}  // namespace stencil::gui

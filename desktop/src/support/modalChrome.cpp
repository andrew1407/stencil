#include "modalChrome.hpp"
#include "iconSet.hpp"

#include <QColor>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Browser .settings-header / .settings-body / .settings-footer padding
    // (components.css: 14px 18px / 14px 18px / 12px 18px).
    constexpr int kPadX = 18;
    constexpr int kHeaderPadY = 12;
    constexpr int kBodyPadY = 14;
    constexpr int kFooterPadY = 12;
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
    if (!iconName.isEmpty()) btn->setIcon(themedIcon(iconName, QColor("#ffffff"), 15));
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
    c.close->setIcon(themedIcon("x", dlg->palette().color(QPalette::WindowText), 14));
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
    return c;
  }

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
      okBtn->setIcon(themedIcon(spec.confirmIcon, QColor("#ffffff"), 14));
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
    if (dlg.exec() != QDialog::Accepted) return ConfirmChoice::Cancel;
    return altPicked ? ConfirmChoice::Alt : ConfirmChoice::Confirm;
  }

  bool confirmModal(QWidget* parent, const ConfirmSpec& spec) {
    return confirmModalChoice(parent, spec) == ConfirmChoice::Confirm;
  }

  QHBoxLayout* addModalFooter(ModalChrome& chrome, const QString& hint) {
    QWidget* dlg = chrome.root ? chrome.root->parentWidget() : nullptr;
    chrome.root->addWidget(modalDivider(dlg));
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(kPadX, kFooterPadY, kPadX, kFooterPadY);
    footer->setSpacing(8);
    if (!hint.isEmpty()) {
      auto* h = new QLabel(hint, dlg);
      h->setObjectName(QStringLiteral("modalFooterHint"));
      h->setWordWrap(true);
      // The hint owns ALL the slack (no competing stretch): a word-wrapped label's
      // minimum is tiny, so splitting the row with a stretch squeezed it into a
      // four-line column while the browser's runs the full width (user report).
      footer->addWidget(h, 1);
    } else {
      footer->addStretch(1);
    }
    chrome.root->addLayout(footer);
    return footer;
  }

}  // namespace stencil::gui

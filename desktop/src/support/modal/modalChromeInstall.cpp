#include "modalChrome.hpp"
#include "modalChromeShared.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"
#include "ShimmerOverlay.hpp"

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

  namespace {
    // Frameless (browser parity), so the HEADER is the drag handle.
    class HeaderDrag : public QObject {
     public:
      HeaderDrag(QWidget* header, QDialog* dlg) : QObject(header), dlg(dlg) {
        header->installEventFilter(this);
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        auto* me = static_cast<QMouseEvent*>(e);
        switch (e->type()) {
          // The cursor has to tell the truth about a header that will not move.
          case QEvent::Enter:
            if (auto* w = qobject_cast<QWidget*>(o))
              w->setCursor(movable() ? Qt::OpenHandCursor : Qt::ArrowCursor);
            break;
          case QEvent::MouseButtonPress:
            if (movable() && me->button() == Qt::LeftButton) {
              grab = me->globalPosition().toPoint() - dlg->frameGeometry().topLeft();
              on = true;
            }
            break;
          case QEvent::MouseMove:
            if (on && dlg) dlg->move(me->globalPosition().toPoint() - grab);
            break;
          case QEvent::MouseButtonRelease:
            on = false;
            break;
          default:
            break;
        }
        return QObject::eventFilter(o, e);
      }

     private:
      /* Only a top-level window moves. execMaybePopover reparents this same dialog into the
       * popover overlay as a plain child, where move() reads the GLOBAL points below as
       * parent-relative and throws the panel out of the overlay it is anchored to. */
      bool movable() const { return dlg && dlg->isWindow(); }

      QPointer<QDialog> dlg;
      QPoint grab;
      bool on = false;
    };
  }  // namespace

  ModalChrome installModalChrome(QDialog* dlg, const QString& iconName, const QString& title) {
    ModalChrome c;
    // Translucent window so the shell's rounded corners clip; the QSS half lives in theme.cpp.
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
    // Dividers run edge to edge, so the root carries no margins; 1px inset keeps children off the border.
    c.root->setContentsMargins(1, 1, 1, 1);
    c.root->setSpacing(0);

    auto* headerW = new QWidget(shell);
    headerW->setObjectName(QStringLiteral("modalHeader"));
    headerW->setCursor(Qt::OpenHandCursor);
    new HeaderDrag(headerW, dlg);
    auto* header = new QHBoxLayout(headerW);
    header->setContentsMargins(PAD_X, HEADER_PAD_Y, PAD_X, HEADER_PAD_Y);
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
    c.close->setProperty(SHIMMER_RADIUS_PROPERTY, 13);   // its QSS radius, so the sweep stays inside
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
    c.body->setContentsMargins(PAD_X, BODY_PAD_Y, PAD_X, BODY_PAD_Y);
    c.body->setSpacing(BODY_SPACING);
    c.root->addLayout(c.body, 1);
    // Deferred a turn, because the caller fills the body after this returns.
    installHoverShimmerLater(dlg);
    return c;
  }


  ConfirmChoice confirmModalChoice(QWidget* parent, const ConfirmSpec& spec) {
    QDialog dlg(parent);
    dlg.setObjectName(QStringLiteral("stencilConfirmModal"));
    dlg.setWindowTitle(spec.title);
    ModalChrome chrome = installModalChrome(&dlg, spec.titleIcon, spec.title);
    auto* msg = new QLabel(spec.message, &dlg);
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::NoTextInteraction);
    chrome.body->addWidget(msg);
    chrome.body->addStretch(1);
    QHBoxLayout* footer = addModalFooter(chrome);
    auto* cancelBtn = new QPushButton(spec.cancelLabel, &dlg);
    makeModalCta(cancelBtn, QStringLiteral("x"));
    footer->addWidget(cancelBtn);
    // Browser order: the askAlt third button sits between Cancel and Confirm.
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
    // Enter = Confirm (the browser focuses its confirm button); Escape rejects via QDialog.
    okBtn->setDefault(true);
    okBtn->setAutoDefault(true);
    footer->addWidget(okBtn);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    // Browser: shared .app-modal width, height sized to the question alone.
    dlg.setFixedWidth(MODAL_WIDTH);
    dlg.adjustSize();
    okBtn->setFocus();
    armFlight(dlg, spec.flight);
    if (dlg.exec() != QDialog::Accepted) return ConfirmChoice::CANCEL;
    return altPicked ? ConfirmChoice::ALT : ConfirmChoice::CONFIRM;
  }

  bool confirmModal(QWidget* parent, const ConfirmSpec& spec) {
    return confirmModalChoice(parent, spec) == ConfirmChoice::CONFIRM;
  }
}  // namespace stencil::gui


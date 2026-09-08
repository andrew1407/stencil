#include "modalChrome.hpp"
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

  namespace {
    // Browser .settings-header / .settings-body / .settings-footer padding
    // (components.css: 14px 18px / 14px 18px / 12px 18px).
    constexpr int kPadX = 18;
    constexpr int kHeaderPadY = 12;
    constexpr int kBodyPadY = 14;
    constexpr int kFooterPadY = 12;
    // The narrowest the footer hint will share a row: below it the hint takes its own
    // line above the buttons (FooterWrap) — left-aligned, the buttons packed right under
    // it — rather than shrinking into a column of one-word lines. Browser twin: the
    // hint's `flex: 1 1 110px` basis under `justify-content: flex-end`.
    constexpr int kFooterHintMinW = 110;

    // The buttons' share of a footer row — every visible non-hint widget's minimum plus
    // the gaps between them: the metric the window's width reservation, the wrap and a
    // `width:auto` dialog all size against.
    int footerButtonsWidth(const QHBoxLayout* row, const QWidget* hint, int* count = nullptr) {
      int need = 0, items = 0;
      for (int i = 0; i < row->count(); ++i) {
        QWidget* w = row->itemAt(i)->widget();
        if (!w || w == hint || w->isHidden()) continue;
        need += w->minimumSizeHint().width();
        ++items;
      }
      if (count) *count = items;
      return need + row->spacing() * qMax(0, items - 1);
    }
  }  // namespace

  int modalFooterLineWidth(const ModalChrome& chrome, const QHBoxLayout* footer) {
    if (!footer) return 0;
    int items = 0;
    int need = kPadX * 2 + 2 + footerButtonsWidth(footer, chrome.footerHint, &items);
    QLabel* hint = chrome.footerHint;
    if (hint && !hint->text().isEmpty()) {
      hint->ensurePolished();   // the QSS font, before it is measured
      need += hint->fontMetrics().horizontalAdvance(hint->text());
      if (items > 0) need += footer->spacing();
    }
    return need;
  }

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

  QLabel* modalSectionLabel(const QString& text, QWidget* parent, bool first) {
    QLabel* l = modalSectionLabel(text, parent);
    l->setContentsMargins(0, first ? 0 : 14, 0, 6);
    return l;
  }

  QWidget* modalRow(QWidget* parent, const QString& label, QLayout* content, int labelMinW) {
    auto* row = new QWidget(parent);
    row->setProperty("vsRow", true);
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(4, 7, 4, 7);
    h->setSpacing(12);
    if (!label.isEmpty()) {
      auto* l = new QLabel(label, row);
      l->setProperty("vsLabel", true);
      if (labelMinW > 0) l->setMinimumWidth(labelMinW);
      h->addWidget(l);
    }
    h->addLayout(content, 1);
    return row;
  }

  QWidget* modalRow(QWidget* parent, const QString& label, QWidget* field, bool grow,
                    int labelMinW) {
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    if (!grow) h->addStretch(1);   // justify-content: space-between
    h->addWidget(field, grow ? 1 : 0);
    return modalRow(parent, label, h, labelMinW);
  }

  QLabel* modalEmptyLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("modalEmpty"));
    return l;
  }

  void sizeModalTall(QDialog* dlg, int width) {
    if (!dlg) return;
    // The browser's min(82vh, 760px) (components.css .app-modal): the same absolute
    // ceiling, with the 82% share taken of the screen less a browser's own chrome.
    constexpr int kModalMaxH = 760;
    constexpr int kBrowserChromePx = 85;
    const QScreen* screen = dlg->screen() ? dlg->screen() : QGuiApplication::primaryScreen();
    const int avail = (screen ? screen->availableGeometry().height() : 900) - kBrowserChromePx;
    dlg->setMinimumSize(width, 360);
    dlg->resize(width, qBound(360, int(avail * 0.82), kModalMaxH));
  }

  QLineEdit* addModalSearchBar(ModalChrome& chrome, const QString& placeholder) {
    QWidget* shell = chrome.root ? chrome.root->parentWidget() : nullptr;
    auto* search = new QLineEdit(shell);
    search->setObjectName(QStringLiteral("modalSearch"));
    search->setPlaceholderText(placeholder);
    search->setClearButtonEnabled(true);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(kPadX, 12, kPadX, 6);
    bar->addWidget(search, 1);
    // Under the header (0) and its hairline (1), above the body (2).
    chrome.root->insertLayout(2, bar);
    return search;
  }

  ModalScrollBody makeModalScrollBody(ModalChrome& chrome, int topPad) {
    ModalScrollBody b;
    QWidget* shell = chrome.root ? chrome.root->parentWidget() : nullptr;
    // The body column gives up its padding to the column inside, so the scrollbar
    // rides the shell's own edge.
    chrome.body->setContentsMargins(0, 0, 0, 0);
    b.scroll = new QScrollArea(shell);
    b.scroll->setObjectName(QStringLiteral("modalScroll"));
    b.scroll->setWidgetResizable(true);
    b.scroll->setFrameShape(QFrame::NoFrame);
    b.scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    b.scroll->viewport()->setAutoFillBackground(false);
    b.content = new QWidget(b.scroll);
    b.content->setAutoFillBackground(false);
    b.layout = new QVBoxLayout(b.content);
    b.layout->setContentsMargins(kPadX, topPad, kPadX, kBodyPadY);
    b.layout->setSpacing(0);
    b.scroll->setWidget(b.content);
    chrome.body->addWidget(b.scroll, 1);
    return b;
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
    c.close->setProperty(kShimmerRadiusProperty, 13);   // its QSS radius, so the sweep stays inside
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
      if (spec.password) line->setEchoMode(QLineEdit::Password);
      line->selectAll();
      chrome.body->addWidget(line);
    }
    // The reason a value cannot be saved, under the field (hidden while it can).
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
    // Live validation: Save (and Enter) go dead with the reason while the text is
    // not saveable; the cursor follows, Qt having no `:disabled { cursor }` in QSS.
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
    // A single-line field confirms on Enter (browser parity). The text AREA owns plain
    // Enter — it types a newline — so only Ctrl/⌘+Enter saves from inside it, and the
    // buttons stay out of Qt's default-button chain so Enter never leaks to them.
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
    // The picker row (browser .confirm-choose-row: 12px above, the select full width).
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

  namespace {
    // The browser's `.settings-footer` wraps (flex-wrap): the hint keeps the buttons' row
    // while it can hold its basis, and drops to its own line above them when it can't —
    // left-aligned, the buttons packed RIGHT beneath it. Buttons that still don't fit wrap
    // onto further right-packed lines, so nothing is cut off at the edge. Qt has no
    // wrapping box, so the swaps are done by hand on every resize.
    class FooterWrap : public QObject {
     public:
      FooterWrap(QWidget* host, QVBoxLayout* stack, QHBoxLayout* actions, QLabel* hint)
          : QObject(host), host_(host), stack_(stack), actions_(actions), hint_(hint) {
        host->installEventFilter(this);
      }

      // Run once the caller has added its buttons — and after addModalFooter's width
      // reservation, or the hint wraps for want of room that was about to arrive.
      void apply() {
        const QList<QWidget*> all = buttons();
        if (all.isEmpty()) return;   // the caller has not added its buttons yet
        const int avail = host_->width() - 2 - kPadX * 2;
        const int gap = actions_->spacing();
        // Greedy lines (browser flex-wrap): the first button that would run past the
        // edge opens the next one. Hidden buttons ride along, taking no room.
        QList<QList<QWidget*>> lines{{}};
        int x = 0, firstLineW = 0;
        for (QWidget* w : all) {
          if (!w->isHidden()) {
            const int bw = w->minimumSizeHint().width();
            if (x > 0 && x + gap + bw > avail) { lines.append(QList<QWidget*>()); x = 0; }
            x += (x > 0 ? gap : 0) + bw;
            if (lines.size() == 1) firstLineW = x;
          }
          lines.last().append(w);
        }
        setWrapped(lines.size() > 1 || avail - firstLineW - gap < kFooterHintMinW);
        setLines(lines);
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == host_ && e->type() == QEvent::Resize) apply();
        return QObject::eventFilter(o, e);
      }

     private:
      // Every button in reading order: the actions row's, then the extra lines'.
      QList<QWidget*> buttons() const {
        QList<QWidget*> out;
        const auto take = [&](const QHBoxLayout* row) {
          for (int i = 0; i < row->count(); ++i) {
            QWidget* w = row->itemAt(i)->widget();
            if (w && w != hint_) out.append(w);
          }
        };
        take(actions_);
        for (QHBoxLayout* row : extra_) take(row);
        return out;
      }

      // Hint beside the buttons (leading the row, growing) or on its own line above.
      void setWrapped(bool on) {
        if (on == wrapped_) return;
        wrapped_ = on;
        if (on) {
          actions_->removeWidget(hint_);
          actions_->insertStretch(0, 1);   // the buttons pack RIGHT on a line of their own
          stack_->insertWidget(0, hint_);
        } else {
          stack_->removeWidget(hint_);
          if (actions_->count() > 0 && actions_->itemAt(0)->spacerItem()) delete actions_->takeAt(0);
          actions_->insertWidget(0, hint_, 1);   // addModalFooter's slot: first in the row
        }
      }

      // Re-home the buttons over `lines`: the first line stays the actions row, each
      // further one is a right-packed row of its own under it. A no-op when nothing moves.
      void setLines(const QList<QList<QWidget*>>& lines) {
        QList<QList<QWidget*>> have{{}};
        for (int i = 0; i < actions_->count(); ++i)
          if (QWidget* w = actions_->itemAt(i)->widget(); w && w != hint_) have.last().append(w);
        for (QHBoxLayout* row : extra_) {
          have.append(QList<QWidget*>());
          for (int i = 0; i < row->count(); ++i)
            if (QWidget* w = row->itemAt(i)->widget()) have.last().append(w);
        }
        if (have == lines) return;
        for (const auto& line : have)
          for (QWidget* w : line) {
            if (QLayout* l = layoutOf(w)) l->removeWidget(w);
          }
        for (QHBoxLayout* row : extra_) { stack_->removeItem(row); delete row; }
        extra_.clear();
        for (int i = 0; i < lines.size(); ++i) {
          QHBoxLayout* row = actions_;
          if (i > 0) {
            row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(actions_->spacing());
            row->addStretch(1);
            stack_->addLayout(row);
            extra_.append(row);
          }
          for (QWidget* w : lines[i]) row->addWidget(w);
        }
      }

      QLayout* layoutOf(QWidget* w) const {
        if (actions_->indexOf(w) >= 0) return actions_;
        for (QHBoxLayout* row : extra_)
          if (row->indexOf(w) >= 0) return row;
        return nullptr;
      }

      QWidget* host_;
      QVBoxLayout* stack_;
      QHBoxLayout* actions_;
      QLabel* hint_;
      QList<QHBoxLayout*> extra_;   // the buttons' further lines, when even they don't fit
      bool wrapped_ = false;
    };
  }  // namespace

  QHBoxLayout* addModalFooter(ModalChrome& chrome, const QString& hint, bool liveHint) {
    QWidget* dlg = chrome.root ? chrome.root->parentWidget() : nullptr;
    chrome.root->addWidget(modalDivider(dlg));
    // Hint left, buttons right, on ONE row (browser .settings-footer). The hint takes all
    // the slack, so at a normal width it wraps at most a line or two instead of being
    // squeezed into a tall column of two-word lines (user report, with a picture).
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    if (hint.isEmpty() && !liveHint) {
      footer->setContentsMargins(kPadX, kFooterPadY, kPadX, kFooterPadY);
      footer->addStretch(1);
      chrome.root->addLayout(footer);
      return footer;
    }
    // A hint rides in a wrap-capable stack: its own line above the buttons once the row
    // can no longer hold kFooterHintMinW beside them, the buttons right-packed under it
    // (FooterWrap). The padding moves to the stack so every line shares it.
    auto* stack = new QVBoxLayout;
    stack->setContentsMargins(kPadX, kFooterPadY, kPadX, kFooterPadY);
    stack->setSpacing(8);
    footer->setContentsMargins(0, 0, 0, 0);
    auto* h = new QLabel(hint, dlg);
    h->setObjectName(QStringLiteral("modalFooterHint"));
    chrome.footerHint = h;
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
    stack->addLayout(footer);
    chrome.root->addLayout(stack);
    auto* wrap = dlg ? new FooterWrap(dlg, stack, footer, h) : nullptr;   // `dlg` = the shell
    // …and the WINDOW is what widens to hold the BUTTONS. An explicit setMinimumSize
    // (every dialog sets one) stops SetDefaultConstraint from raising the minimum to what
    // the layout needs, so a wider system font ran the row out of space and drew the hint
    // under the first button. The hint itself needs no reservation — it wraps to its own
    // line instead. Run once the caller has added its buttons, and only ever upwards.
    if (dlg) {
      QWidget* owner = dlg->parentWidget();   // the dialog the shell fills
      QTimer::singleShot(0, dlg, [dlg, owner, footer, h, wrap] {
        QWidget* win = dlg->window();
        // …but only while the dialog IS the window. Worn as a popover it is re-parented
        // into the main window (mainWindow execMaybePopover), whose minimum width is
        // none of this row's business.
        if (win && win == owner) {
          // The stack pads both lines; the root insets 1px a side.
          const int need = kPadX * 2 + 2 + footerButtonsWidth(footer, h);
          if (need > win->minimumWidth()) win->setMinimumWidth(need);
        }
        if (wrap) wrap->apply();
      });
    }
    return footer;
  }

}  // namespace stencil::gui

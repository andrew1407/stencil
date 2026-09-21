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
    // Browser `.settings-footer` flex-wrap, by hand on every resize: the hint drops to
    // its own line when it can't hold its basis; overflowing buttons wrap right-packed.
    class FooterWrap : public QObject {
     public:
      FooterWrap(QWidget* host, QVBoxLayout* stack, QHBoxLayout* actions, QLabel* hint)
          : QObject(host), host(host), stack(stack), actions(actions), hint(hint) {
        host->installEventFilter(this);
      }

      // After addModalFooter's width reservation, or the hint wraps for room about to arrive.
      void apply() {
        const QList<QWidget*> all = buttons();
        if (all.isEmpty()) return;   // the caller has not added its buttons yet
        const int avail = host->width() - 2 - PAD_X * 2;
        const int gap = actions->spacing();
        // Greedy lines (flex-wrap); hidden buttons ride along, taking no room.
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
        setWrapped(lines.size() > 1 || avail - firstLineW - gap < FOOTER_HINT_MIN_W);
        setLines(lines);
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == host && e->type() == QEvent::Resize) apply();
        return QObject::eventFilter(o, e);
      }

     private:
      QList<QWidget*> buttons() const {
        QList<QWidget*> out;
        const auto take = [&](const QHBoxLayout* row) {
          for (int i = 0; i < row->count(); ++i) {
            QWidget* w = row->itemAt(i)->widget();
            if (w && w != hint) out.append(w);
          }
        };
        take(actions);
        for (QHBoxLayout* row : extra) take(row);
        return out;
      }

      void setWrapped(bool on) {
        if (on == wrapped) return;
        wrapped = on;
        if (on) {
          actions->removeWidget(hint);
          actions->insertStretch(0, 1);   // the buttons pack RIGHT on a line of their own
          stack->insertWidget(0, hint);
        } else {
          stack->removeWidget(hint);
          if (actions->count() > 0 && actions->itemAt(0)->spacerItem()) delete actions->takeAt(0);
          actions->insertWidget(0, hint, 1);   // addModalFooter's slot: first in the row
        }
      }

      // The first line stays the actions row; a no-op when nothing moves.
      void setLines(const QList<QList<QWidget*>>& lines) {
        QList<QList<QWidget*>> have{{}};
        for (int i = 0; i < actions->count(); ++i)
          if (QWidget* w = actions->itemAt(i)->widget(); w && w != hint) have.last().append(w);
        for (QHBoxLayout* row : extra) {
          have.append(QList<QWidget*>());
          for (int i = 0; i < row->count(); ++i)
            if (QWidget* w = row->itemAt(i)->widget()) have.last().append(w);
        }
        if (have == lines) return;
        for (const auto& line : have)
          for (QWidget* w : line) {
            if (QLayout* l = layoutOf(w)) l->removeWidget(w);
          }
        for (QHBoxLayout* row : extra) { stack->removeItem(row); delete row; }
        extra.clear();
        for (int i = 0; i < lines.size(); ++i) {
          QHBoxLayout* row = actions;
          if (i > 0) {
            row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(actions->spacing());
            row->addStretch(1);
            stack->addLayout(row);
            extra.append(row);
          }
          for (QWidget* w : lines[i]) row->addWidget(w);
        }
      }

      QLayout* layoutOf(QWidget* w) const {
        if (actions->indexOf(w) >= 0) return actions;
        for (QHBoxLayout* row : extra)
          if (row->indexOf(w) >= 0) return row;
        return nullptr;
      }

      QWidget* host;
      QVBoxLayout* stack;
      QHBoxLayout* actions;
      QLabel* hint;
      QList<QHBoxLayout*> extra;   // the buttons' further lines, when even they don't fit
      bool wrapped = false;
    };
  }  // namespace

  QHBoxLayout* addModalFooter(ModalChrome& chrome, const QString& hint, bool liveHint) {
    QWidget* dlg = chrome.root ? chrome.root->parentWidget() : nullptr;
    chrome.root->addWidget(modalDivider(dlg));
    // Browser .settings-footer: the hint takes all the slack.
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    if (hint.isEmpty() && !liveHint) {
      footer->setContentsMargins(PAD_X, FOOTER_PAD_Y, PAD_X, FOOTER_PAD_Y);
      footer->addStretch(1);
      chrome.root->addLayout(footer);
      return footer;
    }
    // The padding moves to the stack so every line shares it.
    auto* stack = new QVBoxLayout;
    stack->setContentsMargins(PAD_X, FOOTER_PAD_Y, PAD_X, FOOTER_PAD_Y);
    stack->setSpacing(8);
    footer->setContentsMargins(0, 0, 0, 0);
    auto* h = new QLabel(hint, dlg);
    h->setObjectName(QStringLiteral("modalFooterHint"));
    chrome.footerHint = h;
    h->setWordWrap(true);
    h->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // A PREFERENCE, never a hard minimumWidth: a hard one hands the row negative space
    // and the items overlap; Ignored lets the text yield the last pixels instead.
    QSizePolicy sp(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);   // narrower ⇒ taller, so the wrap is never cut off
    h->setSizePolicy(sp);
    footer->addWidget(h, 1);
    stack->addLayout(footer);
    chrome.root->addLayout(stack);
    auto* wrap = dlg ? new FooterWrap(dlg, stack, footer, h) : nullptr;   // `dlg` = the shell
    // An explicit setMinimumSize stops SetDefaultConstraint from raising the minimum to what the
    // layout needs, so a wider system font drew the hint under the first button. Only ever upwards.
    if (dlg) {
      QWidget* owner = dlg->parentWidget();   // the dialog the shell fills
      QTimer::singleShot(0, dlg, [dlg, owner, footer, h, wrap] {
        QWidget* win = dlg->window();
        // Only while the dialog IS the window: as a popover it is re-parented into the main window.
        if (win && win == owner) {
          // The stack pads both lines; the root insets 1px a side.
          const int need = PAD_X * 2 + 2 + footerButtonsWidth(footer, h);
          if (need > win->minimumWidth()) win->setMinimumWidth(need);
        }
        if (wrap) wrap->apply();
      });
    }
    return footer;
  }
}  // namespace stencil::gui


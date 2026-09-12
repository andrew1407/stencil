#pragma once
// The connect list's palette, row metrics and hover card, private to the connectDialog*.cpp TUs.
#include "controlReveal.hpp"

#include <QToolTip>
#include <QStyleOption>
#include <QString>
#include <QSizePolicy>
#include <QPixmap>
#include <QPainter>
#include <QLabel>
#include <QIcon>
#include <QPushButton>
#include "serverClient.hpp"
#include <QColor>
#include <QEvent>
#include <QWidget>

namespace stencil::gui {

  // The flight of the control (Select all) that appears WITH a new row, so the two land together.
  inline constexpr int CONN_ARRIVE_MS = CONTROL_REVEAL_IN_MS;

  // The browser's --remote-gold.
  inline const QColor GOLD("#d4a017");
  // "The credential, not the server, is the problem": the dot, the expired note, the outline.
  inline const QColor AMBER("#e0a800");
  inline const QColor AMBER_HOVER("#c99400");
  // Browser .connect-row, measured: 25px buttons + 8px/10px padding + 1px outline = 43.
  inline constexpr int ROW_HEIGHT = 43;
  inline constexpr int ROW_URL_ROLE = Qt::UserRole + 2;
  inline bool kindMatches(const QString& mode, bool admin) {
    return mode == QLatin1String("all") || (mode == QLatin1String("admin")) == admin;
  }

  // Qt sends Enter/Leave to the child under the pointer, so a bare :hover rule blinks off over a label.
  class RowCard : public QWidget {
   public:
    RowCard() {
      setAttribute(Qt::WA_StyledBackground, true);
      setProperty("hovered", false);
    }
    void watchChildren() {
      for (QWidget* w : findChildren<QWidget*>()) w->installEventFilter(this);
    }

   protected:
    void enterEvent(QEnterEvent* e) override { QWidget::enterEvent(e); syncHover(); }
    void leaveEvent(QEvent* e) override { QWidget::leaveEvent(e); syncHover(); }
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::Enter || e->type() == QEvent::Leave) syncHover();
      return QWidget::eventFilter(o, e);
    }

   private:
    void syncHover() {
      // Covers the gap between two children (Leave lands before the next Enter).
      bool on = underMouse() || rect().contains(mapFromGlobal(QCursor::pos()));
      if (!on)
        for (const QWidget* w : findChildren<QWidget*>())
          if (w->underMouse()) { on = true; break; }
      if (on == property("hovered").toBool()) return;
      setProperty("hovered", on);
      style()->unpolish(this);
      style()->polish(this);
    }
  };

  inline QPixmap statusDot(stencil::net::ServerClient::Status s) {
    typedef stencil::net::ServerClient::Status S;
    // Amber for BOTH connecting and expired: only the credential is missing (browser parity).
    QColor c = s == S::CONNECTED  ? QColor("#28a745")
             : s == S::CONNECTING ? AMBER
             : s == S::EXPIRED    ? AMBER
                                  : QColor("#dc3545");
    // Browser .conn-status: a 9px disc inside a 2px halo of its own colour at 18%.
    QPixmap pm(13, 13);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    QColor halo = c;
    halo.setAlphaF(0.18);
    p.setBrush(halo);
    p.drawEllipse(0, 0, 13, 13);
    p.setBrush(c);
    p.drawEllipse(2, 2, 9, 9);
    return pm;
  }

  // Browser text-overflow; the size hint stays narrow so a long URL can never force a scrollbar.
  // The browser's row actions are ordinary accent-filled .btn-icons; `text` is empty for all but the expired row's fix.
  inline QPushButton* makeRowActionButton(const QIcon& ic, const QString& tip,
                                          const QString& text = QString()) {
    auto* b = new QPushButton(text);
    b->setIcon(ic);
    b->setIconSize(QSize(15, 15));
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("rowAction", true);
    return b;
  }

  class ElidedLabel : public QLabel {
   public:
    explicit ElidedLabel(const QString& text) : QLabel(text), full_(text) {
      setToolTip(full_);
      setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    QSize sizeHint() const override { return QSize(40, QLabel::sizeHint().height()); }
    QSize minimumSizeHint() const override {
      return QSize(40, QLabel::minimumSizeHint().height());
    }

   protected:
    void resizeEvent(QResizeEvent* e) override {
      QLabel::resizeEvent(e);
      setText(fontMetrics().elidedText(full_, Qt::ElideMiddle, width()));
    }

   private:
    QString full_;
  };


}  // namespace stencil::gui

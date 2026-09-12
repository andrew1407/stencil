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
#include "serverClient.hpp"
#include <QColor>
#include <QEvent>
#include <QWidget>

namespace stencil::gui {

  // A new row's gather — the flight of the control (Select all) that appears WITH it, so
  // the two land together (rebuildList says why). Its removal keeps kConnMs.
  inline constexpr int kConnArriveMs = kControlRevealInMs;

  // The collaboration gold used for server points throughout the app (mirrors the
  // browser's --remote-gold), so shared servers read the same on every front-end.
  inline const QColor kGold("#d4a017");
  // The amber of "the credential, not the server, is the problem": the dot, the
  // expired note, and that row's outline.
  inline const QColor kAmber("#e0a800");
  // The amber's own hover shade (browser .connect-expired .connect-reconnect-one:hover).
  inline const QColor kAmberHover("#c99400");
  // Row height: the browser's .connect-row, measured — 25px buttons inside its
  // 8px/10px padding and 1px outline come to 43.
  inline constexpr int kRowHeight = 43;
  // The row's url, on the item (Qt::UserRole is the kind filter's admin flag).
  inline constexpr int kRowUrlRole = Qt::UserRole + 2;
  // The kind picker's rule: All, or the row's admin flag agreeing with the pick.
  inline bool kindMatches(const QString& mode, bool admin) {
    return mode == QLatin1String("all") || (mode == QLatin1String("admin")) == admin;
  }

  // A row that hovers as ONE card: Qt sends Enter/Leave to the child under the
  // pointer, so a bare :hover rule blinks off over a label.
  class RowCard : public QWidget {
   public:
    RowCard() {
      setAttribute(Qt::WA_StyledBackground, true);  // a bare QWidget won't paint one
      setProperty("hovered", false);
    }
    // Call once the row's children exist.
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
      // The cursor test covers the gap between two children (Leave lands before the
      // next Enter); underMouse() is what a synthesized hover sets.
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

  // A filled status dot: green=connected, amber=connecting, red=error — mirrors the
  // browser's connection-status dot.
  inline QPixmap statusDot(stencil::net::ServerClient::Status s) {
    using S = stencil::net::ServerClient::Status;
    // Amber for BOTH "connecting" and "expired": the server is fine either way,
    // only the credential is missing (browser parity — an expired row is amber,
    // never the red of an unreachable host).
    QColor c = s == S::Connected  ? QColor("#28a745")
             : s == S::Connecting ? kAmber
             : s == S::Expired    ? kAmber
                                  : QColor("#dc3545");
    // Browser .conn-status: a 9px disc inside a 2px halo of its own colour at 18%
    // (box-shadow: 0 0 0 2px color-mix(currentColor 18%, transparent)).
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

  // A label that elides to whatever width the row gives it (browser: CSS
  // text-overflow). Its size hint stays narrow so a long URL can never force a
  // horizontal scrollbar; the full text lives on the tooltip.
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

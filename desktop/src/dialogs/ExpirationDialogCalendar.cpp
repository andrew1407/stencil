#include "../support/SearchCombo.hpp"
#include "expirationDialogParts.hpp"
#include "ExpirationDialog.hpp"
#include "iconSet.hpp"

#include "../support/modalChrome.hpp"  // the browser modal shell + its confirm/prompt

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace stencil::gui {

  void ExpirationDialog::renderControls() {
    periodRow_->setEnabled(!keep());   // takes the select + Refresh with it
    calendar_->setEnabled(!keep());
    today_->setText(tr("Today: <b>%1</b>").arg(fmtDate(dateFromMs(nowMs_))));
    when_->setText(keep() ? tr("Expires: <b>never (kept forever)</b>")
                         : tr("Expires: <b>%1</b>").arg(fmtDate(dateFromMs(expiresAt_))));
  }

  void ExpirationDialog::renderCalendar() {
    calTitle_->setText(QString("%1 %2").arg(MONTHS[viewM_]).arg(viewY_));
    prev_->setEnabled(!atFloor());   // no navigating into fully-past months

    // Rebuilt whole, like the browser rebuilds the grid's innerHTML: the day states are
    // QSS property selectors, and re-polishing 42 live buttons is the longer road.
    while (QLayoutItem* item = calGrid_->takeAt(0)) {
      if (QWidget* w = item->widget()) w->deleteLater();
      delete item;
    }
    for (int i = 0; i < 7; ++i) {
      auto* h = new QLabel(WEEKDAYS[i], calendar_);
      h->setObjectName(QStringLiteral("expCalWeekday"));
      h->setAlignment(Qt::AlignCenter);
      calGrid_->addWidget(h, 0, i);
    }

    const QDate today = dateFromMs(nowMs_);
    const QDate first(viewY_, viewM_ + 1, 1);
    const int lead = first.dayOfWeek() - 1;   // Monday-first, like the browser grid
    const int days = first.daysInMonth();
    const QDate expiry = (!keep() && expiresAt_) ? dateFromMs(expiresAt_) : QDate();
    for (int d = 1; d <= days; ++d) {
      const QDate cellDate(viewY_, viewM_ + 1, d);
      const int slot = lead + d - 1;
      auto* cell = new QToolButton(calendar_);
      cell->setObjectName(QStringLiteral("expCalDay"));
      cell->setText(QString::number(d));
      cell->setCursor(Qt::PointingHandCursor);
      cell->setMinimumHeight(CELL_MIN_H);
      cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      if (cellDate == expiry) cell->setProperty("expDay", "expiry");
      else if (cellDate == today) cell->setProperty("expDay", "today");
      if (cellDate < today) {
        cell->setEnabled(false);
        cell->setCursor(Qt::ArrowCursor);
      } else {
        connect(cell, &QToolButton::clicked, this, [this, cellDate] {
          if (keep()) return;
          expiresAt_ = msEndOfDay(cellDate);
          renderAll();
        });
      }
      calGrid_->addWidget(cell, 1 + slot / 7, slot % 7);
    }
  }

  bool ExpirationDialog::keep() const { return keepBox_ && keepBox_->isChecked(); }

  long long ExpirationDialog::expiresAtMs() const { return keep() ? 0 : expiresAt_; }
  QString ExpirationDialog::refreshPeriod() const {
    return period_->currentData().toString();
  }
  bool ExpirationDialog::autoRefresh() const { return auto_->isChecked(); }
}


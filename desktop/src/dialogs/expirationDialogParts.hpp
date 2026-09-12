#pragma once
// The expiry presets, calendar labels and day-boundary maths, private to the expirationDialog*.cpp TUs.
#include "modalChrome.hpp"
#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>

#include <QDate>
#include <QLayout>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace stencil::gui {

  inline constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;
  // The browser modal is narrower than the shared 560px shell (.exp-modal: 400px):
  // the widest thing in it is the calendar, and at full width it sat in a band of
  // dead space either side.
  inline constexpr int kExpWidth = 400;
  inline constexpr int kCellMinH = 26;   // .exp-cal-cell min-height

  // Period presets, mirroring browser projectsStore.js PERIOD_ORDER + labels.
  struct Preset { const char* key; const char* label; };
  const Preset kPresets[] = {
      {"day", "1 day"},
      {"week", "1 week"},
      {"fortnight", "2 weeks (fortnight)"},
      {"month", "1 month"},
      {"3month", "3 months"},
      {"6month", "6 months"},
      {"year", "1 year"},
  };
  const char* const kWeekdays[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  const char* const kMonths[] = {"January", "February", "March", "April", "May", "June",
                                 "July", "August", "September", "October", "November",
                                 "December"};

  inline QDate dateFromMs(long long ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).date();
  }
  // Expires through the whole picked day (local 23:59:59.999).
  inline long long msEndOfDay(const QDate& d) {
    return d.startOfDay().toMSecsSinceEpoch() + DAY_MS - 1;
  }
  inline QString fmtDate(const QDate& d) {
    return QLocale().toString(d, QLocale::ShortFormat);
  }

  // One browser .vs-row: a hairline-underlined form row. The QSS half ([vsRow]) lives
  // in theme.cpp; the row's contents differ per caller, so it takes a laid-out widget.
  inline QWidget* vsRow(QWidget* parent, QLayout* content) {
    content->setSpacing(12);   // the row's own gap, between its children too
    return modalRow(parent, QString(), content);
  }

  // One .exp-legend-item: a 12px outlined chip beside "<caption>: <b>value</b>".
  inline QWidget* legendItem(QWidget* parent, const QString& swatchId, QLabel** out) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* chip = new QFrame(w);
    chip->setObjectName(swatchId);
    chip->setFixedSize(12, 12);
    h->addWidget(chip);
    auto* text = new QLabel(w);
    text->setObjectName(QStringLiteral("expLegend"));
    text->setTextFormat(Qt::RichText);
    h->addWidget(text);
    *out = text;
    return w;
  }

}  // namespace stencil::gui

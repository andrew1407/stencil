#include "../support/searchCombo.hpp"
#include "expirationDialog.hpp"
#include "iconSet.hpp"

#include "../support/modalChrome.hpp"  // the browser modal shell + its confirm/prompt
#include "projectsStore.hpp"

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

  namespace {
    constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;
    // The browser modal is narrower than the shared 560px shell (.exp-modal: 400px):
    // the widest thing in it is the calendar, and at full width it sat in a band of
    // dead space either side.
    constexpr int kExpWidth = 400;
    constexpr int kCellMinH = 26;   // .exp-cal-cell min-height

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

    QDate dateFromMs(long long ms) {
      return QDateTime::fromMSecsSinceEpoch(ms).date();
    }
    // Expires through the whole picked day (local 23:59:59.999).
    long long msEndOfDay(const QDate& d) {
      return d.startOfDay().toMSecsSinceEpoch() + DAY_MS - 1;
    }
    QString fmtDate(const QDate& d) {
      return QLocale().toString(d, QLocale::ShortFormat);
    }

    // One browser .vs-row: a hairline-underlined form row. The QSS half ([vsRow]) lives
    // in theme.cpp; the row's contents differ per caller, so it takes a laid-out widget.
    QWidget* vsRow(QWidget* parent, QLayout* content) {
      content->setSpacing(12);   // the row's own gap, between its children too
      return modalRow(parent, QString(), content);
    }

    // One .exp-legend-item: a 12px outlined chip beside "<caption>: <b>value</b>".
    QWidget* legendItem(QWidget* parent, const QString& swatchId, QLabel** out) {
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
  }  // namespace

  ExpirationDialog::ExpirationDialog(const QString& projectName, long long expiresAt,
                                     const QString& refreshPeriod, bool autoRefresh,
                                     long long nowMs, QWidget* parent)
      : QDialog(parent), nowMs_(nowMs), expiresAt_(expiresAt) {
    ModalChrome chrome = installModalChrome(this, QStringLiteral("calendar"),
                                            tr("Project expiration"));
    const QColor ink = palette().color(QPalette::WindowText);

    auto* nameLbl = new QLabel(projectName.isEmpty() ? tr("Untitled") : projectName, this);
    nameLbl->setObjectName(QStringLiteral("expProjectName"));
    chrome.body->addWidget(nameLbl);

    keepBox_ = new QCheckBox(tr("Keep forever (never expires)"), this);
    auto* keepRow = new QHBoxLayout;
    keepRow->addWidget(keepBox_);
    keepRow->addStretch(1);
    chrome.body->addWidget(vsRow(this, keepRow));

    // "Expires in" + the period select and Refresh button, as one .vs-row.
    auto* periodLay = new QHBoxLayout;
    auto* periodLbl = new QLabel(tr("Expires in"), this);
    periodLbl->setProperty("vsLabel", true);
    periodLay->addWidget(periodLbl);
    periodLay->addStretch(1);
    period_ = new SearchComboBox(this, /*searchable=*/false);
    for (const auto& p : kPresets) period_->addItem(p.label, QString::fromLatin1(p.key));
    periodLay->addWidget(period_);
    refresh_ = new QPushButton(tr("Refresh"), this);
    makeModalCta(refresh_, QStringLiteral("refresh"));
    refresh_->setToolTip(tr("Set the expiration to now + the selected period"));
    periodLay->addWidget(refresh_);
    periodRow_ = vsRow(this, periodLay);
    chrome.body->addWidget(periodRow_);

    auto_ = new QCheckBox(tr("Refresh expiration each time the project is opened"), this);
    auto* autoRow = new QHBoxLayout;
    autoRow->addWidget(auto_);
    autoRow->addStretch(1);
    chrome.body->addWidget(vsRow(this, autoRow));

    // The month card: a ‹ title › head over a 7-column day grid, rebuilt per month.
    calendar_ = new QFrame(this);
    calendar_->setObjectName(QStringLiteral("expCalendar"));
    calendar_->setAttribute(Qt::WA_StyledBackground, true);
    auto* calV = new QVBoxLayout(calendar_);
    calV->setContentsMargins(10, 8, 10, 8);
    calV->setSpacing(6);
    auto* headLay = new QHBoxLayout;
    headLay->setContentsMargins(0, 0, 0, 0);
    prev_ = new QToolButton(calendar_);
    prev_->setObjectName(QStringLiteral("expCalNav"));
    prev_->setIcon(themedIcon("chevron-left", ink, 16));
    prev_->setToolTip(tr("Previous month"));
    next_ = new QToolButton(calendar_);
    next_->setObjectName(QStringLiteral("expCalNav"));
    next_->setIcon(themedIcon("chevron-right", ink, 16));
    next_->setToolTip(tr("Next month"));
    calTitle_ = new QLabel(calendar_);
    calTitle_->setObjectName(QStringLiteral("expCalTitle"));
    calTitle_->setAlignment(Qt::AlignCenter);
    headLay->addWidget(prev_);
    headLay->addWidget(calTitle_, 1);
    headLay->addWidget(next_);
    calV->addLayout(headLay);
    calGrid_ = new QGridLayout;
    calGrid_->setContentsMargins(0, 0, 0, 0);
    calGrid_->setSpacing(2);
    calV->addLayout(calGrid_);
    chrome.body->addWidget(calendar_);

    auto* legend = new QHBoxLayout;
    legend->setContentsMargins(0, 0, 0, 0);
    legend->setSpacing(16);
    legend->addWidget(legendItem(this, QStringLiteral("expSwatchToday"), &today_));
    legend->addWidget(legendItem(this, QStringLiteral("expSwatchExpiry"), &when_));
    legend->addStretch(1);
    chrome.body->addLayout(legend);
    chrome.body->addStretch(1);

    QHBoxLayout* footer = addModalFooter(
        chrome, tr("Past dates can’t be chosen. Expiration is local to this app."));
    auto* saveBtn = new QPushButton(tr("Save"), this);
    makeModalCta(saveBtn, QStringLiteral("check"));
    saveBtn->setDefault(true);
    footer->addWidget(saveBtn);
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Seed initial control state from the passed-in project meta, then paint once.
    const int idx = period_->findData(refreshPeriod.isEmpty() ? QStringLiteral("week")
                                                              : refreshPeriod);
    period_->setCurrentIndex(idx < 0 ? period_->findData(QStringLiteral("week")) : idx);
    auto_->setChecked(autoRefresh);
    keepBox_->setChecked(expiresAt_ == 0);
    setViewToExpiry();
    renderAll();

    // Wire signals AFTER the initial state so setup doesn't trigger seeding.
    connect(period_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { seedFromPeriod(); });
    connect(refresh_, &QPushButton::clicked, this, &ExpirationDialog::seedFromPeriod);
    connect(keepBox_, &QCheckBox::toggled, this, &ExpirationDialog::toggleKeepForever);
    connect(prev_, &QToolButton::clicked, this, [this] {
      if (atFloor()) return;
      if (--viewM_ < 0) { viewM_ = 11; viewY_--; }
      renderCalendar();
    });
    connect(next_, &QToolButton::clicked, this, [this] {
      if (++viewM_ > 11) { viewM_ = 0; viewY_++; }
      renderCalendar();
    });

    setFixedWidth(kExpWidth);
    adjustSize();
  }

  // The calendar floor is the current month: every earlier day is disabled, so
  // navigating below it is pointless (browser parity: expirationModal.js atFloor).
  bool ExpirationDialog::atFloor() const {
    const QDate t = dateFromMs(nowMs_);
    return viewY_ < t.year() || (viewY_ == t.year() && viewM_ + 1 <= t.month());
  }

  void ExpirationDialog::setViewToExpiry() {
    const QDate d = expiresAt_ ? dateFromMs(expiresAt_) : dateFromMs(nowMs_);
    viewY_ = d.year();
    viewM_ = d.month() - 1;
    // An already-expired project's date is in the past; don't open below the floor.
    const QDate t = dateFromMs(nowMs_);
    if (viewY_ < t.year() || (viewY_ == t.year() && viewM_ + 1 < t.month())) {
      viewY_ = t.year();
      viewM_ = t.month() - 1;
    }
  }

  void ExpirationDialog::seedFromPeriod() {
    if (keep()) return;
    const QString key = period_->currentData().toString();
    expiresAt_ = core::ProjectsStore::addPeriod(nowMs_, key.toStdString());
    setViewToExpiry();
    renderAll();
  }

  void ExpirationDialog::toggleKeepForever() {
    if (keepBox_->isChecked()) {
      ConfirmSpec spec;
      spec.title = tr("Keep forever");
      spec.message = tr("Keep this project forever and remove its expiration date?");
      spec.confirmIcon = QStringLiteral("calendar");
      if (!confirmModal(this, spec)) {
        QSignalBlocker b(keepBox_);
        keepBox_->setChecked(false);
        return;
      }
      expiresAt_ = 0;
    } else {
      const QString key = period_->currentData().toString();
      expiresAt_ = core::ProjectsStore::addPeriod(nowMs_, key.toStdString());
      setViewToExpiry();
    }
    renderAll();
  }

  void ExpirationDialog::renderControls() {
    periodRow_->setEnabled(!keep());   // takes the select + Refresh with it
    calendar_->setEnabled(!keep());
    today_->setText(tr("Today: <b>%1</b>").arg(fmtDate(dateFromMs(nowMs_))));
    when_->setText(keep() ? tr("Expires: <b>never (kept forever)</b>")
                         : tr("Expires: <b>%1</b>").arg(fmtDate(dateFromMs(expiresAt_))));
  }

  void ExpirationDialog::renderCalendar() {
    calTitle_->setText(QString("%1 %2").arg(kMonths[viewM_]).arg(viewY_));
    prev_->setEnabled(!atFloor());   // no navigating into fully-past months

    // Rebuilt whole, like the browser rebuilds the grid's innerHTML: the day states are
    // QSS property selectors, and re-polishing 42 live buttons is the longer road.
    while (QLayoutItem* item = calGrid_->takeAt(0)) {
      if (QWidget* w = item->widget()) w->deleteLater();
      delete item;
    }
    for (int i = 0; i < 7; ++i) {
      auto* h = new QLabel(kWeekdays[i], calendar_);
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
      cell->setMinimumHeight(kCellMinH);
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

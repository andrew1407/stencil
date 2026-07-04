#include "expirationDialog.hpp"

#include "guiHelpers.hpp"
#include "projectsStore.hpp"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextCharFormat>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;

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
  }  // namespace

  ExpirationDialog::ExpirationDialog(const QString& projectName, long long expiresAt,
                                     const QString& refreshPeriod, bool autoRefresh,
                                     long long nowMs, QWidget* parent)
      : QDialog(parent), nowMs_(nowMs), expiresAt_(expiresAt) {
    setWindowTitle("Project expiration");
    setMinimumWidth(340);

    auto* layout = new QVBoxLayout(this);

    auto* nameLbl = new QLabel(projectName.isEmpty() ? "Untitled" : projectName, this);
    nameLbl->setStyleSheet("font-weight: 600;");
    layout->addWidget(nameLbl);

    keep_ = new QCheckBox("Keep forever (never expires)", this);
    layout->addWidget(keep_);

    // Period + Refresh row.
    auto* periodRow = new QHBoxLayout;
    periodRow->addWidget(new QLabel("Expires in:", this));
    period_ = new QComboBox(this);
    for (const auto& p : kPresets) period_->addItem(p.label, QString::fromLatin1(p.key));
    periodRow->addWidget(period_, 1);
    refresh_ = new QPushButton("Refresh", this);
    refresh_->setToolTip("Set the expiration to now + the selected period");
    periodRow->addWidget(refresh_);
    layout->addLayout(periodRow);

    auto_ = new QCheckBox("Refresh expiration each time the project is opened", this);
    layout->addWidget(auto_);

    calendar_ = new QCalendarWidget(this);
    calendar_->setMinimumDate(dateFromMs(nowMs_));  // no past dates
    calendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    // Qt paints weekends in an aggressive red by default; match them to the normal
    // weekday text colour so the grid reads calmly (today/expiry still stand out).
    QTextCharFormat weekdayFmt;
    weekdayFmt.setForeground(calendar_->palette().color(QPalette::Text));
    calendar_->setWeekdayTextFormat(Qt::Saturday, weekdayFmt);
    calendar_->setWeekdayTextFormat(Qt::Sunday, weekdayFmt);
    layout->addWidget(calendar_);

    today_ = new QLabel(this);
    when_ = new QLabel(this);
    today_->setStyleSheet("color: gray; font-size: 11px;");
    when_->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(today_);
    layout->addWidget(when_);

    // Seed initial control state from the passed-in project meta.
    const int idx = period_->findData(refreshPeriod.isEmpty()
                                          ? QStringLiteral("week")
                                          : refreshPeriod);
    period_->setCurrentIndex(idx < 0 ? period_->findData(QStringLiteral("week")) : idx);
    auto_->setChecked(autoRefresh);
    keep_->setChecked(expiresAt_ == 0);
    if (expiresAt_ != 0) {
      QSignalBlocker b(calendar_);
      calendar_->setSelectedDate(dateFromMs(expiresAt_));
    }
    repaintCalendar();
    syncControls();

    // Wire signals AFTER the initial state so setup doesn't trigger seeding.
    connect(period_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { seedFromPeriod(); });
    connect(refresh_, &QPushButton::clicked, this, &ExpirationDialog::seedFromPeriod);
    connect(keep_, &QCheckBox::toggled, this, &ExpirationDialog::toggleKeepForever);
    connect(calendar_, &QCalendarWidget::clicked, this, [this](const QDate& d) {
      if (keep_->isChecked()) return;
      expiresAt_ = msEndOfDay(d);
      repaintCalendar();
      syncControls();
    });

    auto* box = makeButtonBox(this, QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText("Save");
    layout->addWidget(box);
  }

  void ExpirationDialog::seedFromPeriod() {
    if (keep_->isChecked()) return;
    const QString key = period_->currentData().toString();
    expiresAt_ = core::ProjectsStore::addPeriod(nowMs_, key.toStdString());
    {
      QSignalBlocker b(calendar_);
      calendar_->setSelectedDate(dateFromMs(expiresAt_));
    }
    repaintCalendar();
    syncControls();
  }

  void ExpirationDialog::toggleKeepForever() {
    if (keep_->isChecked()) {
      const auto r = QMessageBox::question(
          this, "Keep forever",
          "Keep this project forever and remove its expiration date?");
      if (r != QMessageBox::Yes) {
        QSignalBlocker b(keep_);
        keep_->setChecked(false);
        return;
      }
      expiresAt_ = 0;
    } else {
      const QString key = period_->currentData().toString();
      expiresAt_ = core::ProjectsStore::addPeriod(nowMs_, key.toStdString());
      QSignalBlocker b(calendar_);
      calendar_->setSelectedDate(dateFromMs(expiresAt_));
    }
    repaintCalendar();
    syncControls();
  }

  void ExpirationDialog::repaintCalendar() {
    calendar_->setDateTextFormat(QDate(), QTextCharFormat());  // reset all

    QTextCharFormat todayFmt;
    todayFmt.setFontWeight(QFont::Bold);
    todayFmt.setForeground(QColor("#16a34a"));       // today outline colour (green)
    todayFmt.setBackground(QColor(22, 163, 74, 40));
    calendar_->setDateTextFormat(dateFromMs(nowMs_), todayFmt);

    if (!keep_->isChecked() && expiresAt_ != 0) {
      QTextCharFormat expFmt;
      expFmt.setFontWeight(QFont::Bold);
      expFmt.setForeground(QColor("#7c3aed"));        // expiry outline colour (accent)
      expFmt.setBackground(QColor(124, 58, 237, 55));
      calendar_->setDateTextFormat(dateFromMs(expiresAt_), expFmt);
    }
  }

  void ExpirationDialog::syncControls() {
    const bool keep = keep_->isChecked();
    period_->setEnabled(!keep);
    refresh_->setEnabled(!keep);
    calendar_->setEnabled(!keep);
    today_->setText("Today: " + fmtDate(dateFromMs(nowMs_)));
    when_->setText(keep ? "Expires: never (kept forever)"
                        : "Expires: " + fmtDate(dateFromMs(expiresAt_)));
  }

  long long ExpirationDialog::expiresAtMs() const {
    return keep_->isChecked() ? 0 : expiresAt_;
  }
  QString ExpirationDialog::refreshPeriod() const {
    return period_->currentData().toString();
  }
  bool ExpirationDialog::autoRefresh() const { return auto_->isChecked(); }

}

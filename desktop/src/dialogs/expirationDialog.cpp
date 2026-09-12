#include "../support/searchCombo.hpp"
#include "expirationDialogParts.hpp"
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
    for (const auto& p : PRESETS) period_->addItem(p.label, QString::fromLatin1(p.key));
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

    setFixedWidth(EXP_WIDTH);
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
}


#include "../../support/control/dblReset.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "expirationDialogParts.hpp"
#include "ExpirationDialog.hpp"
#include "iconSet.hpp"

#include "../../support/modal/modalChrome.hpp"  // the browser modal shell + its confirm/prompt
#include "ProjectsStore.hpp"

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
      : QDialog(parent), nowMs(nowMs), expiresAt(expiresAt) {
    ModalChrome chrome = installModalChrome(this, QStringLiteral("calendar"),
                                            tr("Project expiration"));
    const QColor ink = palette().color(QPalette::WindowText);

    auto* nameLbl = new QLabel(projectName.isEmpty() ? tr("Untitled") : projectName, this);
    nameLbl->setObjectName(QStringLiteral("expProjectName"));
    chrome.body->addWidget(nameLbl);

    keepBox = new QCheckBox(tr("Keep forever (never expires)"), this);
    support::setResetDefault(keepBox, false);
    auto* keepRow = new QHBoxLayout;
    keepRow->addWidget(keepBox);
    keepRow->addStretch(1);
    chrome.body->addWidget(vsRow(this, keepRow));

    // "Expires in" + the period select and Refresh button, as one .vs-row.
    auto* periodLay = new QHBoxLayout;
    auto* periodLbl = new QLabel(tr("Expires in"), this);
    periodLbl->setProperty("vsLabel", true);
    periodLay->addWidget(periodLbl);
    periodLay->addStretch(1);
    period = new SearchComboBox(this, /*searchable=*/false);
    for (const auto& p : PRESETS) period->addItem(p.label, QString::fromLatin1(p.key));
    support::setResetDefault(period, QStringLiteral("week"));   // browser projectPeriods.js DEFAULT_PERIOD
    periodLay->addWidget(period);
    refresh = new QPushButton(tr("Refresh"), this);
    makeModalCta(refresh, QStringLiteral("refresh"));
    refresh->setToolTip(tr("Set the expiration to now + the selected period"));
    periodLay->addWidget(refresh);
    periodRow = vsRow(this, periodLay);
    chrome.body->addWidget(periodRow);

    auto_ = new QCheckBox(tr("Refresh expiration each time the project is opened"), this);
    support::setResetDefault(auto_, true);
    auto* autoRow = new QHBoxLayout;
    autoRow->addWidget(auto_);
    autoRow->addStretch(1);
    chrome.body->addWidget(vsRow(this, autoRow));

    // The month card: a ‹ title › head over a 7-column day grid, rebuilt per month.
    calendar = new QFrame(this);
    calendar->setObjectName(QStringLiteral("expCalendar"));
    calendar->setAttribute(Qt::WA_StyledBackground, true);
    auto* calV = new QVBoxLayout(calendar);
    calV->setContentsMargins(10, 8, 10, 8);
    calV->setSpacing(6);
    auto* headLay = new QHBoxLayout;
    headLay->setContentsMargins(0, 0, 0, 0);
    prev = new QToolButton(calendar);
    prev->setObjectName(QStringLiteral("expCalNav"));
    prev->setIcon(themedIcon("chevron-left", ink, 16));
    prev->setToolTip(tr("Previous month"));
    next = new QToolButton(calendar);
    next->setObjectName(QStringLiteral("expCalNav"));
    next->setIcon(themedIcon("chevron-right", ink, 16));
    next->setToolTip(tr("Next month"));
    calTitle = new QLabel(calendar);
    calTitle->setObjectName(QStringLiteral("expCalTitle"));
    calTitle->setAlignment(Qt::AlignCenter);
    headLay->addWidget(prev);
    headLay->addWidget(calTitle, 1);
    headLay->addWidget(next);
    calV->addLayout(headLay);
    calGrid = new QGridLayout;
    calGrid->setContentsMargins(0, 0, 0, 0);
    calGrid->setSpacing(2);
    calV->addLayout(calGrid);
    chrome.body->addWidget(calendar);

    auto* legend = new QHBoxLayout;
    legend->setContentsMargins(0, 0, 0, 0);
    legend->setSpacing(16);
    legend->addWidget(legendItem(this, QStringLiteral("expSwatchToday"), &today));
    legend->addWidget(legendItem(this, QStringLiteral("expSwatchExpiry"), &when));
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
    const int idx = period->findData(refreshPeriod.isEmpty() ? QStringLiteral("week")
                                                              : refreshPeriod);
    period->setCurrentIndex(idx < 0 ? period->findData(QStringLiteral("week")) : idx);
    auto_->setChecked(autoRefresh);
    keepBox->setChecked(this->expiresAt == 0);
    setViewToExpiry();
    renderAll();

    // Wire signals AFTER the initial state so setup doesn't trigger seeding.
    connect(period, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { seedFromPeriod(); });
    connect(refresh, &QPushButton::clicked, this, &ExpirationDialog::seedFromPeriod);
    connect(keepBox, &QCheckBox::toggled, this, &ExpirationDialog::toggleKeepForever);
    connect(prev, &QToolButton::clicked, this, [this] {
      if (atFloor()) return;
      if (--viewM < 0) { viewM = 11; viewY--; }
      renderCalendar();
    });
    connect(next, &QToolButton::clicked, this, [this] {
      if (++viewM > 11) { viewM = 0; viewY++; }
      renderCalendar();
    });

    setFixedWidth(EXP_WIDTH);
    adjustSize();
  }

  // The calendar floor is the current month: every earlier day is disabled, so
  // navigating below it is pointless (browser parity: expirationModal.js atFloor).
  bool ExpirationDialog::atFloor() const {
    const QDate t = dateFromMs(nowMs);
    return viewY < t.year() || (viewY == t.year() && viewM + 1 <= t.month());
  }

  void ExpirationDialog::setViewToExpiry() {
    const QDate d = expiresAt ? dateFromMs(expiresAt) : dateFromMs(nowMs);
    viewY = d.year();
    viewM = d.month() - 1;
    // An already-expired project's date is in the past; don't open below the floor.
    const QDate t = dateFromMs(nowMs);
    if (viewY < t.year() || (viewY == t.year() && viewM + 1 < t.month())) {
      viewY = t.year();
      viewM = t.month() - 1;
    }
  }

  void ExpirationDialog::seedFromPeriod() {
    if (keep()) return;
    const QString key = period->currentData().toString();
    expiresAt = core::ProjectsStore::addPeriod(nowMs, key.toStdString());
    setViewToExpiry();
    renderAll();
  }

  void ExpirationDialog::toggleKeepForever() {
    if (keepBox->isChecked()) {
      ConfirmSpec spec;
      spec.title = tr("Keep forever");
      spec.message = tr("Keep this project forever and remove its expiration date?");
      spec.confirmIcon = QStringLiteral("calendar");
      if (!confirmModal(this, spec)) {
        QSignalBlocker b(keepBox);
        keepBox->setChecked(false);
        return;
      }
      expiresAt = 0;
    } else {
      const QString key = period->currentData().toString();
      expiresAt = core::ProjectsStore::addPeriod(nowMs, key.toStdString());
      setViewToExpiry();
    }
    renderAll();
  }
}


#pragma once
#include <QDialog>
#include <QString>

class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// Project-expiration editor. Mirrors browser/js/ui/expirationModal.js: a refresh-
// period selector + Refresh button seed "now + period", a QCalendarWidget lets the
// user pick any future day (today and the expiry day highlighted in two colors,
// past days disabled), a "keep forever" checkbox (confirmed) clears the date, and
// an "auto-refresh on open" checkbox restarts the window each time the project is
// opened. exec(), then read expiresAtMs()/refreshPeriod()/autoRefresh(). Local only.
namespace stencil::gui {

  class ExpirationDialog : public QDialog {
    Q_OBJECT
   public:
    ExpirationDialog(const QString& projectName, long long expiresAt,
                     const QString& refreshPeriod, bool autoRefresh,
                     long long nowMs, QWidget* parent = nullptr);

    long long expiresAtMs() const;      // 0 == keep forever
    QString refreshPeriod() const;
    bool autoRefresh() const;

   private:
    void seedFromPeriod();              // expiresAt = now + selected period
    void toggleKeepForever();
    void repaintCalendar();
    void syncControls();

    long long nowMs_ = 0;
    long long expiresAt_ = 0;           // working value (0 = keep forever)

    QCheckBox* keep_ = nullptr;
    QComboBox* period_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QCheckBox* auto_ = nullptr;
    QCalendarWidget* calendar_ = nullptr;
    QLabel* today_ = nullptr;
    QLabel* when_ = nullptr;
  };

}

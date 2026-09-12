#pragma once
#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QWidget;

// Project-expiration editor, mirroring browser/js/ui/expirationModal.js chrome and all:
// a refresh-period selector + Refresh seeding "now + period", a hand-built month grid
// (not QCalendarWidget, whose nav bar and weekday header are nothing like the browser's),
// a confirmed "keep forever" that clears the date, and an "auto-refresh on open".
// exec(), then read expiresAtMs()/refreshPeriod()/autoRefresh(). Local only.
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
    bool keep() const;                  // keep-forever — the checkbox IS the state
    void seedFromPeriod();              // expiresAt = now + selected period
    void toggleKeepForever();
    bool atFloor() const;               // the view month is the current one (or earlier)
    void setViewToExpiry();
    void renderControls();
    void renderCalendar();
    void renderAll() { renderControls(); renderCalendar(); }

    long long nowMs_ = 0;
    long long expiresAt_ = 0;           // working value (0 = keep forever)
    int viewY_ = 0;                     // the month the grid is showing
    int viewM_ = 0;

    QCheckBox* keepBox_ = nullptr;
    QWidget* periodRow_ = nullptr;
    QComboBox* period_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QCheckBox* auto_ = nullptr;
    QFrame* calendar_ = nullptr;
    QLabel* calTitle_ = nullptr;
    QGridLayout* calGrid_ = nullptr;
    QToolButton* prev_ = nullptr;
    QToolButton* next_ = nullptr;
    QLabel* today_ = nullptr;
    QLabel* when_ = nullptr;
  };

}

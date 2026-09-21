#pragma once
#include "accentDefaults.hpp"
#include <QColor>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

// Transient toasts. Port of browser/js/ui/notifications.js; stacks upward from the host's bottom-left.
namespace stencil::gui {

  class DisintegrateOverlay;

  class Notifications : public QObject {
    Q_OBJECT
   public:
    enum class Level { INFO, SUCCESS, ERROR };

    // The browser shows ONE balloon; a fourth arrival retires the oldest early.
    static constexpr int MAX_VISIBLE = 3;

    explicit Notifications(QWidget* host);

    // TWO colours: red for a failure, the accent for everything else (browser .notify-ok/.notify-info).
    void setColors(const QColor& normal, const QColor& error);

    // MainWindow hands it the status bar's height, so the stack sits ON the canvas.
    void setBottomInset(int px);
    // The stack sits beside a left-docked chat and its dust is clipped to the free side.
    void setLeftInset(int px);

    void info(const QString& text) { show(text, Level::INFO); }
    void success(const QString& text) { show(text, Level::SUCCESS); }
    void error(const QString& text) { show(text, Level::ERROR); }

    // `special` is a logo show's own notice: the egg on gold, whatever the accent.
    void show(const QString& text, Level level, int msec = 3000, bool special = false);
    // The newest toast still standing, for a caller that decorates its own notice.
    QLabel* lastToast() const { return stack.isEmpty() ? nullptr : stack.last().data(); }

   protected:
    // The host is still narrow when the first toast appears at startup.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void reflow();
    // Idempotent: a toast retired early still has its auto-dismiss timer pending.
    void dismiss(QLabel* toast);
    QList<QLabel*> liveToasts() const;

    QWidget* host = nullptr;
    QColor normalBg{DEFAULT_ACCENT_HEX}, errorBg{"#d6293e"};   // light-theme defaults
    int bottomInset = 0;
    int leftInset = 0;
    // Insertion order is the ONLY reliable "oldest": reflow() raise()s each toast, which
    // moves it to the end of the child list. QPointer, so a deleted toast drops out.
    QList<QPointer<QLabel>> stack;
    // The still-flying entrance cloud, so reflow() can drag it along to a new slot.
    QHash<QLabel*, QPointer<DisintegrateOverlay>> entering;
  };

}

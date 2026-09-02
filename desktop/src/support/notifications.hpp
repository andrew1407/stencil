#pragma once
#include <QColor>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

// Transient toast notifications. Port of browser/js/ui/notifications.js: a small
// message that appears, then auto-dismisses after a few seconds. Toasts stack
// upward from the bottom-left of the host widget.
namespace stencil::gui {

  class DisintegrateOverlay;

  class Notifications : public QObject {
    Q_OBJECT
   public:
    enum class Level { Info, Success, Error };

    // The stack never grows past this: the browser shows ONE balloon at a time, and the
    // desktop's stacking meant a repeated action (flipping the theme a few times) walled
    // off the bottom-left corner of the canvas with a column of identical toasts. A
    // fourth arrival retires the oldest early instead of piling on.
    static constexpr int kMaxVisible = 3;

    explicit Notifications(QWidget* host);

    // Re-colour the toasts to the active theme. TWO colours, not one per level: red means
    // something went wrong, everything else carries the theme accent (browser parity — see
    // .notify-ok/.notify-info in css/components.css). Without this the levels were three
    // fixed hexes that matched neither theme, including a green that sat outside the palette.
    void setColors(const QColor& normal, const QColor& error);

    // Extra clearance above the host's bottom edge — MainWindow hands it the status bar's
    // height, so the stack sits ON the canvas rather than across the coord readout.
    void setBottomInset(int px);
    // The width a left-docked chat panel occupies — the stack sits beside it (with its own
    // gap) and its dust is clipped to the free side (MainWindow::syncToastInset).
    void setLeftInset(int px);

    void info(const QString& text) { show(text, Level::Info); }
    void success(const QString& text) { show(text, Level::Success); }
    void error(const QString& text) { show(text, Level::Error); }

    void show(const QString& text, Level level, int msec = 3000);

   protected:
    // Recenter live toasts when the host (scroll viewport) resizes — at startup
    // the host is still narrow when the first toast appears, so without this a
    // toast positioned then would stay off-center / clipped after the window
    // reaches its final size.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void reflow();
    // Play a toast out and delete it. Idempotent: a toast retired early by the cap above
    // still has its own auto-dismiss timer pending, and that must not restage the exit.
    void dismiss(QLabel* toast);
    // Toasts on screen, oldest first — the ones already leaving don't count.
    QList<QLabel*> liveToasts() const;

    QWidget* host_ = nullptr;
    QColor normalBg_{"#7c3aed"}, errorBg_{"#d6293e"};   // light-theme defaults
    int bottomInset_ = 0;
    int leftInset_ = 0;
    // Insertion order, which is the ONLY reliable source of "oldest". The obvious
    // host_->findChildren<QLabel*>("toast") is not: reflow() raise()s each toast, and
    // raise() moves a widget to the end of its parent's child list — so that order flips
    // on every reflow, and the cap below happily retired the NEWEST toasts.
    // QPointer, so a toast deleted by any other route drops out on its own.
    QList<QPointer<QLabel>> stack_;
    // The still-flying entrance cloud for a toast whose fade hasn't landed yet, so
    // reflow() can drag it along when a burst bumps the toast to a new slot mid-flight —
    // without this the cloud stays pinned to the box it was grabbed at, and the widget
    // reappears somewhere else with no motes to show for it.
    QHash<QLabel*, QPointer<DisintegrateOverlay>> entering_;
  };

}

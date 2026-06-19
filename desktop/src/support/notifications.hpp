#pragma once
#include <QObject>
#include <QString>

class QWidget;

// Transient toast notifications. Port of browser/js/ui/notifications.js: a small
// message that appears, then auto-dismisses after a few seconds. Toasts stack
// downward from the top-center of the host widget.
namespace stencil::gui {

  class Notifications : public QObject {
    Q_OBJECT
   public:
    enum class Level { Info, Success, Error };

    explicit Notifications(QWidget* host);

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

    QWidget* host_ = nullptr;
  };

}

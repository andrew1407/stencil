#include "notifications.hpp"
#include <algorithm>
#include <QEvent>
#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace stencil::gui {

  Notifications::Notifications(QWidget* host) : QObject(host), host_(host) {
    // Watch the host so toasts recenter when it resizes (see eventFilter).
    if (host_) host_->installEventFilter(this);
  }

  void Notifications::show(const QString& text, Level level, int msec) {
    if (!host_) return;

    // Colors mirror the browser toast variants (info/success/error).
    QString bg;
    switch (level) {
      case Level::Success: bg = "#28a745"; break;
      case Level::Error:   bg = "#dc3545"; break;
      case Level::Info:    bg = "#7c3aed"; break;
    }

    auto* toast = new QLabel(text, host_);
    toast->setObjectName("toast");
    toast->setStyleSheet(QString("QLabel#toast { background: %1; color: white; "
                                 "padding: 8px 14px; border-radius: 6px; }")
                             .arg(bg));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Resolve the stylesheet (padding/font) before measuring; otherwise
    // adjustSize() sizes against the unstyled label and the text gets clipped.
    toast->ensurePolished();
    toast->adjustSize();
    toast->show();
    toast->raise();

    reflow();

    QTimer::singleShot(msec, toast, [this, toast] {
      toast->deleteLater();
      // Reflow after the event loop deletes it.
      QTimer::singleShot(0, this, [this] { reflow(); });
    });
  }

  // Stack every live toast vertically, centered near the top of the host. The x
  // is clamped to a small margin so a toast wider than the (briefly narrow)
  // host is never pushed off the left edge and clipped.
  void Notifications::reflow() {
    if (!host_) return;
    int y = 12;
    const auto toasts = host_->findChildren<QLabel*>("toast",
                                                     Qt::FindDirectChildrenOnly);
    for (QLabel* t : toasts) {
      const int x = std::max(8, (host_->width() - t->width()) / 2);
      t->move(x, y);
      t->raise();
      y += t->height() + 8;
    }
  }

  bool Notifications::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host_ && event->type() == QEvent::Resize) reflow();
    return QObject::eventFilter(watched, event);
  }

}

// Click-outside dismissal of a modal (browser ui/base.js) and its STENCIL_MODAL_LOG trace: the
// application-wide press watcher every platform runs. macOS adds the native half, modalDismissMac.mm.
#include "modalDismiss.hpp"
#include "modalReveal.hpp"   // installGestureClock

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QWidget>

namespace stencil::support {

  bool modalDismissLogOn() {
    static const bool on = !qEnvironmentVariableIsEmpty("STENCIL_MODAL_LOG");
    return on;
  }

  void modalDismissLog(const QString& line) {
    if (!modalDismissLogOn()) return;
    QFile f(QString::fromLocal8Bit(qgetenv("STENCIL_MODAL_LOG")));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream(&f) << line << '\n';
  }

  namespace {
    constexpr const char* MODAL_DISMISS_FILTER_NAME = "stencilModalDismissFilter";

    // Application-wide: the press goes to a BLOCKED window and QApplication drops it, but
    // an application filter still sees it first.
    class ModalDismissFilter : public QObject {
     public:
      explicit ModalDismissFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(MODAL_DISMISS_FILTER_NAME));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::MouseButtonPress) return QObject::eventFilter(o, e);
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* w = qobject_cast<QWidget*>(o);
        if (modalDismissLogOn())
          modalDismissLog(QStringLiteral("[modal] Qt press on %1, modal=%2")
                              .arg(QString::fromLatin1(w ? w->metaObject()->className()
                                                         : o->metaObject()->className()),
                                   QString::fromLatin1(dlg ? dlg->metaObject()->className() : "(none)")));
        if (!dlg || !w || !dlg->isVisible() || qobject_cast<QFileDialog*>(dlg)
            || dlg->property(NO_OUTSIDE_DISMISS_PROPERTY).toBool())
          return QObject::eventFilter(o, e);
        // Popups keep the widget they were built from as parent, so the walk reaches the dialog.
        for (const QWidget* p = w; p; p = p->parentWidget())
          if (p == dlg) return QObject::eventFilter(o, e);
        dlg->reject();
        return true;   // swallowed, like the overlay eating the click in the browser
      }
    };
  }  // namespace

  void installModalDismiss() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;
    if (app->findChild<QObject*>(QString::fromLatin1(MODAL_DISMISS_FILTER_NAME),
                                 Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new ModalDismissFilter(app));
    installGestureClock(app);
    installModalDismissNative();
  }

  // WEAK so headless binaries link without modalDismissMac.mm; MSVC has no weak symbols.
#if defined(_MSC_VER)
  void installModalDismissNative() {}
#else
  __attribute__((weak)) void installModalDismissNative() {}
#endif

}  // namespace stencil::support

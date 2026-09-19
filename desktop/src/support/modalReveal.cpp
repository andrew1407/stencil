// Modal reveal/dismiss: a dialog arrives and leaves as a surface (support/DisintegrateOverlay),
// and the file/colour pickers the app raises ride the same flight. Split across modalReveal*.hpp.
#include "modalRevealFlight.hpp"

#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

namespace stencil::support {

  void revealDialog(QDialog& dlg, QWidget* anchor) { revealDialog(dlg, anchor, QRect()); }

  // A scroll area decides its scrollbar on a posted layout pass that grab() runs ahead of.
  void settleLayout(QWidget& w) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    if (QLayout* l = w.layout()) l->activate();
    for (QAbstractScrollArea* area : w.findChildren<QAbstractScrollArea*>()) {
      QResizeEvent ev(area->size(), area->size());
      QCoreApplication::sendEvent(area, &ev);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  }

  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect,
                           const std::function<void(const QColor&)>& preview, bool withAlpha,
                           const QRect& closeRect) {
    // Non-native: the macOS shared panel misbehaves under our event filters.
    QColorDialog dlg(parent);
    dlg.setOption(QColorDialog::DontUseNativeDialog);
    // Alpha only where the caller stores it: CSS `#rrggbbaa` (support/cssColor.hpp).
    if (withAlpha) dlg.setOption(QColorDialog::ShowAlphaChannel);
    dlg.setWindowTitle(title);
    dlg.setCurrentColor(initial);
    // exec() centres an unpositioned QDialog; revealDialog reads geometry after layout.
    if (preview) {
      QObject::connect(&dlg, &QColorDialog::currentColorChanged, &dlg,
                       [&preview](const QColor& c) { if (c.isValid()) preview(c); });
    }
    revealDialog(dlg, anchor, anchorRect, closeRect);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (!accepted && preview) preview(initial);
    return accepted ? dlg.selectedColor() : QColor();
  }

  void revealWindow(QWidget& w, QWidget* anchor) {
    if (motionReduced()) return;
    flyWindow(w, anchor, true, nullptr);
  }

  void dismissWindow(QWidget& w, QWidget* anchor) {
    if (motionReduced()) { w.hide(); return; }
    QPointer<QWidget> guard(&w);
    flyWindow(w, anchor, false, nullptr);
    w.hide();
  }

  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect) { revealDialog(dlg, anchor, anchorRect, QRect()); }
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect, const QRect& closeRect) {
    dlg.setProperty(REVEALED_PROPERTY, true);
    // A WINDOW dims and blurs what it covers; the popover flies itself and stays undimmed.
    ModalBackdrop::behindAll(&dlg);
    flyDialog(dlg, anchor, anchorRect, closeRect);
  }

  namespace {
    // The browser's GESTURE_ANCHOR_PX, so a question forms out of the gesture on both surfaces.
    constexpr int GESTURE_ANCHOR_PX = 26;
    constexpr const char* DIALOG_REVEAL_FILTER_NAME = "stencilDialogRevealFilter";

    // One application-wide watcher: `QMessageBox::question(this, …)` has nowhere to hang a reveal off.
    class DialogRevealFilter : public QObject {
     public:
      explicit DialogRevealFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(DIALOG_REVEAL_FILTER_NAME));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Show) return QObject::eventFilter(o, e);
        auto* dlg = qobject_cast<QDialog*>(o);
        auto* fileDlg = qobject_cast<QFileDialog*>(dlg);
        if (!dlg || dlg->property(REVEALED_PROPERTY).toBool()
            || dlg->property(NO_DIALOG_REVEAL_PROPERTY).toBool()
            // A NATIVE panel is placed by the OS; DontUseNativeDialog opts back in.
            || (fileDlg && !fileDlg->testOption(QFileDialog::DontUseNativeDialog)))
          return QObject::eventFilter(o, e);
        // A keyboard-raised question has no fresh point; a stale cursor is a gesture that never happened.
        flyDialog(*dlg, nullptr, gestureAnchorRect());
        return QObject::eventFilter(o, e);
      }
    };
  }  // namespace

  QRect gestureAnchorRect() {
    const QPoint p = QCursor::pos();
    return QRect(p.x() - GESTURE_ANCHOR_PX / 2, p.y() - GESTURE_ANCHOR_PX / 2,
                 GESTURE_ANCHOR_PX, GESTURE_ANCHOR_PX);
  }

  void modalDismissLog(const QString& line) {
    const QByteArray path = qgetenv("STENCIL_MODAL_LOG");
    if (path.isEmpty()) return;
    QFile f(QString::fromLocal8Bit(path));
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
    installModalDismissNative();
  }

  // WEAK so headless binaries link without modalDismissMac.mm; MSVC has no weak symbols.
#if defined(_MSC_VER)
  void installModalDismissNative() {}
#else
  __attribute__((weak)) void installModalDismissNative() {}
#endif

  void installDialogReveal() {
    QCoreApplication* app = QCoreApplication::instance();
    // Offscreen has no compositor for windowOpacity. Reduced motion is re-read per flight.
    if (!app || QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    if (app->findChild<QObject*>(QString::fromLatin1(DIALOG_REVEAL_FILTER_NAME),
                                 Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new DialogRevealFilter(app));
  }

}  // namespace stencil::support

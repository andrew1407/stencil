#include "controlSwap.hpp"

namespace stencil::gui {

  // A combo's text changed. Only a PICK animates: a repopulation (the item count moved)
  // and the first value a combo ever shows go straight up, or every dialog would deal
  // itself in on open.
  void ctl::onComboText(QComboBox* cb, const QString& to) {
    const QVariant prev = cb->property(kValueSwapTextProperty);
    const int prevCount = cb->property(kValueSwapCountProperty).toInt();
    const QString from = prev.toString();
    rememberComboValue(cb);   // the cache is the TRUE current value from here on
    if (cb->property(kNoControlSwapProperty).toBool()) return;
    // An EDITABLE combo (the zoom box, the LLM model box) must not animate per
    // KEYSTROKE — but a PICK from its list is a value exchange like any other, so
    // the outgoing value is stashed for onComboPick (textActivated fires after
    // currentTextChanged has already overwritten the cache).
    if (cb->isEditable()) {
      cb->setProperty(kValueSwapPrevProperty, from);
      return;
    }
    if (support::motionReduced() || !cb->isVisible()) {
      ValueSwapOverlay::cancel(cb);
      return;
    }
    if (!prev.isValid() || from.isEmpty() || to.isEmpty() || from == to) return;
    if (prevCount != cb->count()) return;
    ValueSwapOverlay::play(cb, from, to);
  }


  // A PICK from an editable combo's dropped list (the zoom presets, the model box):
  // the one moment such a combo exchanges values rather than being typed into —
  // browser parity: the zoom preset pick plays markSwap on its input.
  void ctl::onComboPick(QComboBox* cb, const QString& to) {
    if (!cb->isEditable()) return;   // non-editables animate via currentTextChanged
    if (cb->property(kNoControlSwapProperty).toBool()) return;
    if (support::motionReduced() || !cb->isVisible()) {
      ValueSwapOverlay::cancel(cb);
      return;
    }
    const QString from = cb->property(kValueSwapPrevProperty).toString();
    if (from.isEmpty() || to.isEmpty() || from == to) return;
    ValueSwapOverlay::play(cb, from, to);
  }

  ControlSwapFilter::ControlSwapFilter(QObject* parent) : QObject(parent) {
    setObjectName(QString::fromLatin1(kControlSwapFilterName));
  }

  // Install the watcher on the application. Idempotent — every MainWindow calls it, and
  // only the first one takes.
  void installControlSwap() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app
        || app->findChild<QObject*>(QString::fromLatin1(kControlSwapFilterName),
                                    Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new ControlSwapFilter(app));
  }
}  // namespace stencil::gui

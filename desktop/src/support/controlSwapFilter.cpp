#include "controlSwap.hpp"

namespace stencil::gui {

  // Only a PICK animates: a repopulation or the first value a combo shows goes straight up.
  void ctl::onComboText(QComboBox* cb, const QString& to) {
    const QVariant prev = cb->property(VALUE_SWAP_TEXT_PROPERTY);
    const int prevCount = cb->property(VALUE_SWAP_COUNT_PROPERTY).toInt();
    const QString from = prev.toString();
    rememberComboValue(cb);   // the cache is the TRUE current value from here on
    if (cb->property(NO_CONTROL_SWAP_PROPERTY).toBool()) return;
    // An EDITABLE combo must not animate per KEYSTROKE, only a PICK from its list —
    // the outgoing value is stashed for onComboPick.
    if (cb->isEditable()) {
      cb->setProperty(VALUE_SWAP_PREV_PROPERTY, from);
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


  // The one moment an editable combo exchanges values rather than being typed into
  // (browser parity: the zoom preset pick plays markSwap).
  void ctl::onComboPick(QComboBox* cb, const QString& to) {
    if (!cb->isEditable()) return;   // non-editables animate via currentTextChanged
    if (cb->property(NO_CONTROL_SWAP_PROPERTY).toBool()) return;
    if (support::motionReduced() || !cb->isVisible()) {
      ValueSwapOverlay::cancel(cb);
      return;
    }
    const QString from = cb->property(VALUE_SWAP_PREV_PROPERTY).toString();
    if (from.isEmpty() || to.isEmpty() || from == to) return;
    ValueSwapOverlay::play(cb, from, to);
  }

  ControlSwapFilter::ControlSwapFilter(QObject* parent) : QObject(parent) {
    setObjectName(QString::fromLatin1(CONTROL_SWAP_FILTER_NAME));
  }

  // Idempotent — every MainWindow calls it; only the first one takes.
  void installControlSwap() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app
        || app->findChild<QObject*>(QString::fromLatin1(CONTROL_SWAP_FILTER_NAME),
                                    Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new ControlSwapFilter(app));
  }
}  // namespace stencil::gui

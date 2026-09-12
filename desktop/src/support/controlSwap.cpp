#include "controlSwap.hpp"

namespace stencil::gui {


  QRect ctl::indicatorRect(const QCheckBox* box) {
    QStyleOptionButton opt;
    opt.initFrom(box);
    opt.rect = box->rect();
    return box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, box);
  }


  // The indicator as the style would paint it in `checked`. RENDERED, not grabbed:
  // photographing both states off the live widget would mean forcing a state onto it
  // mid-signal, and a checkbox in a button group cannot be poked like that safely.
  QPixmap ctl::indicatorPixmap(QCheckBox* box, const QRect& r, bool checked) {
    const qreal dpr = box->devicePixelRatioF();
    QPixmap pm(QSize(qRound(r.width() * dpr), qRound(r.height() * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QStyleOptionButton opt;
    opt.initFrom(box);
    opt.rect = QRect(QPoint(0, 0), r.size());
    // QStyleSheetStyle reads :checked off the OPTION's state, so both looks come from
    // one live widget. Sunken is dropped — a snapshot must not be caught mid-press.
    opt.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange
                   | QStyle::State_Sunken);
    opt.state |= checked ? QStyle::State_On : QStyle::State_Off;
    QPainter p(&pm);
    box->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &opt, &p, box);
    return pm;
  }


  // Drop the scatter `box` has in flight. The real indicator is already in its true
  // state underneath, so cancelling can never strand a stale check.
  void ctl::cancelCheckSwap(QCheckBox* box) {
    QWidget* host = box->window();
    if (!host) return;
    const auto live = host->findChildren<QWidget*>(QString::fromLatin1(kCheckSwapObjectName));
    for (QWidget* w : live)
      if (w->property(kCheckSwapOwnerProperty).value<QObject*>() == box) delete w;
  }


  // Play the indicator's arrival/departure. `checked` is the state the box has JUST
  // reached, so a check GATHERS and an uncheck SCATTERS. Reduced motion, a hidden box
  // and an indicator too small to grid (the f(x,y) pill hides its own) all do nothing —
  // the state itself has already changed, which is the part that must never be dropped.
  void swapCheckIndicator(QCheckBox* box, bool checked) {
    if (!box || box->property(kNoControlSwapProperty).toBool()) return;
    ctl::cancelCheckSwap(box);
    if (!box->isVisible() || support::motionReduced()) return;
    QWidget* host = box->window();
    if (!host) return;
    const QRect r = ctl::indicatorRect(box);
    if (r.width() < 6 || r.height() < 6) return;
    const QPixmap on = ctl::indicatorPixmap(box, r, true);
    const QPixmap off = ctl::indicatorPixmap(box, r, false);
    // A control that says nothing with its indicator has nothing to scatter — the f(x,y)
    // pill hides its tick and carries the state in the whole chip's fill.
    if (on.toImage() == off.toImage()) return;
    const QRect at(box->mapTo(host, r.topLeft()), r.size());
    DisintegrateOverlay* fx = DisintegrateOverlay::overPixmaps(
        on, off, at, host,
        checked ? DisintegrateOverlay::Sweep::Gather : DisintegrateOverlay::Sweep::Fall,
        kCheckSwapCells, kCheckSwapCells, kCheckSwapMs, kCheckSwapSpread, kCheckSwapPadPx,
        QString::fromLatin1(kCheckSwapObjectName));
    if (fx) fx->setProperty(kCheckSwapOwnerProperty, QVariant::fromValue<QObject*>(box));
  }

  QStyleOptionComboBox ctl::comboOption(const QComboBox* cb, const QString& text) {
    QStyleOptionComboBox o;
    o.initFrom(cb);
    o.rect = cb->rect();
    o.subControls = QStyle::SC_All;
    o.editable = cb->isEditable();
    o.frame = cb->hasFrame();
    o.iconSize = cb->iconSize();
    o.currentText = text;
    return o;
  }


  // Where the chosen option is drawn, in the combo's own coordinates.
  QRect ctl::comboFieldRect(const QComboBox* cb) {
    QStyleOptionComboBox o = comboOption(cb, cb->currentText());
    const QRect r =
        cb->style()->subControlRect(QStyle::CC_ComboBox, &o, QStyle::SC_ComboBoxEditField, cb);
    return r.isValid() && !r.isEmpty() ? r : cb->rect();
  }


  // The combo's LABEL alone, drawn by the style into a transparent, control-sized
  // pixmap — so the word is the one the combo would paint, in the colour the theme
  // gives it, with no palette guessing. Must run BEFORE the label is hidden: the same
  // widget stylesheet that blanks the real text would blank this too.
  QPixmap ctl::comboLabelPixmap(QComboBox* cb, const QString& text) {
    const qreal dpr = cb->devicePixelRatioF();
    QPixmap pm(QSize(qRound(cb->width() * dpr), qRound(cb->height() * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (cb->isEditable()) {
      // An editable combo's word lives in its QLineEdit — CE_ComboBoxLabel paints
      // nothing for it, so draw the value by hand in the field's own font/colour.
      p.setFont(cb->font());
      p.setPen(cb->palette().color(QPalette::Text));
      p.drawText(comboFieldRect(cb).adjusted(3, 0, -2, 0),
                 Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
      return pm;
    }
    QStyleOptionComboBox o = comboOption(cb, text);
    cb->style()->drawControl(QStyle::CE_ComboBoxLabel, &o, &p, cb);
    return pm;
  }

  void ctl::repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
  }


  // Blank the REAL label for the duration, so only the overlay's two words are ever on
  // screen. A widget stylesheet is the only lever that beats the app-wide
  // `QComboBox { color: … }`; the property selector gives it that rule's weight, and
  // every state is listed so a hover mid-swap can't outrank it. Colour only — nothing
  // in the box model is touched, so the control cannot change size.
  void ctl::hideComboLabel(QComboBox* cb, bool hide) {
    if (hide == cb->property(kValueSwapProperty).toBool()) return;
    if (hide) {
      cb->setProperty(kValueSwapSheetProperty, cb->styleSheet());
      cb->setProperty(kValueSwapProperty, true);
      const QString p = QString::fromLatin1(kValueSwapProperty);
      cb->setStyleSheet(cb->property(kValueSwapSheetProperty).toString()
                        + QStringLiteral("QComboBox[%1=\"true\"],QComboBox[%1=\"true\"]:hover,"
                                         "QComboBox[%1=\"true\"]:focus,"
                                         "QComboBox[%1=\"true\"]:on,"
                                         "QComboBox[%1=\"true\"] QLineEdit"
                                         "{color:rgba(0,0,0,0);}")
                              .arg(p));
    } else {
      cb->setProperty(kValueSwapProperty, false);
      cb->setStyleSheet(cb->property(kValueSwapSheetProperty).toString());
    }
    // Qt matches property selectors at POLISH time, so both edges have to re-polish or
    // the rule is skipped entirely and the old word stays up under the swap.
    repolish(cb);
  }

  ctl::ComboPopupDust::ComboPopupDust(QComboBox* cb) : QObject(cb), cb_(cb) {
    setObjectName(QString::fromLatin1(kComboPopupFilterName));
  }

  bool ctl::ComboPopupDust::eventFilter(QObject* o, QEvent* e) {
    if (!cb_) return QObject::eventFilter(o, e);
    auto* popup = qobject_cast<QWidget*>(o);
    if (!popup) return QObject::eventFilter(o, e);
    if (e->type() == QEvent::Show) {
      support::revealPopup(*popup, cb_);
    } else if (e->type() == QEvent::Hide) {
      // Same reasoning as the Show branch: Qt hides its own container, so the
      // close has to hang off that Hide too, or it never played an exit flight.
      support::dismissPopup(*popup, cb_);
    }
    return QObject::eventFilter(o, e);
  }


  bool ControlSwapFilter::eventFilter(QObject* o, QEvent* e) {
    const QEvent::Type type = e->type();
    if (type != QEvent::Show && type != QEvent::Polish)
      return QObject::eventFilter(o, e);
    if (o->property(kControlSwapWiredProperty).toBool()) return QObject::eventFilter(o, e);
    if (auto* box = qobject_cast<QCheckBox*>(o)) {
      box->setProperty(kControlSwapWiredProperty, true);
      connect(box, &QCheckBox::toggled, box,
              [box](bool on) { swapCheckIndicator(box, on); });
    } else if (auto* cb = qobject_cast<QComboBox*>(o)) {
      cb->setProperty(kControlSwapWiredProperty, true);
      ctl::rememberComboValue(cb);
      ctl::wireComboPopupDust(cb);
      connect(cb, &QComboBox::currentTextChanged, cb,
              [cb](const QString& to) { ctl::onComboText(cb, to); });
      connect(cb, &QComboBox::textActivated, cb,
              [cb](const QString& to) { ctl::onComboPick(cb, to); });
    }
    return QObject::eventFilter(o, e);
  }


  // Arm it once per combo. view()->window() is the container Qt drops; asking for the
  // view is what creates it, which is exactly why this can be done at wire time.
  void ctl::wireComboPopupDust(QComboBox* cb) {
    QAbstractItemView* view = cb->view();
    QWidget* popup = view ? view->window() : nullptr;
    if (!popup || popup == cb->window()) return;
    // The watcher is parented to the COMBO (the popup container is Qt's, and outlives
    // nothing of ours), so that is where the once-only guard looks.
    if (cb->findChild<QObject*>(QString::fromLatin1(kComboPopupFilterName),
                                Qt::FindDirectChildrenOnly))
      return;
    popup->installEventFilter(new ComboPopupDust(cb));
  }

  void ctl::rememberComboValue(QComboBox* cb) {
    cb->setProperty(kValueSwapTextProperty, cb->currentText());
    cb->setProperty(kValueSwapCountProperty, cb->count());
  }
}  // namespace stencil::gui

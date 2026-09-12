#include "controlSwap.hpp"

namespace stencil::gui {


  QRect ctl::indicatorRect(const QCheckBox* box) {
    QStyleOptionButton opt;
    opt.initFrom(box);
    opt.rect = box->rect();
    return box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, box);
  }


  // RENDERED, not grabbed: forcing a state onto a live box mid-signal is unsafe.
  QPixmap ctl::indicatorPixmap(QCheckBox* box, const QRect& r, bool checked) {
    const qreal dpr = box->devicePixelRatioF();
    QPixmap pm(QSize(qRound(r.width() * dpr), qRound(r.height() * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QStyleOptionButton opt;
    opt.initFrom(box);
    opt.rect = QRect(QPoint(0, 0), r.size());
    // QStyleSheetStyle reads :checked off the OPTION's state; Sunken must not be caught mid-press.
    opt.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange
                   | QStyle::State_Sunken);
    opt.state |= checked ? QStyle::State_On : QStyle::State_Off;
    QPainter p(&pm);
    box->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &opt, &p, box);
    return pm;
  }


  void ctl::cancelCheckSwap(QCheckBox* box) {
    QWidget* host = box->window();
    if (!host) return;
    const auto live = host->findChildren<QWidget*>(QString::fromLatin1(CHECK_SWAP_OBJECT_NAME));
    for (QWidget* w : live)
      if (w->property(CHECK_SWAP_OWNER_PROPERTY).value<QObject*>() == box) delete w;
  }


  // `checked` is the state just reached: a check GATHERS, an uncheck SCATTERS.
  void swapCheckIndicator(QCheckBox* box, bool checked) {
    if (!box || box->property(NO_CONTROL_SWAP_PROPERTY).toBool()) return;
    ctl::cancelCheckSwap(box);
    if (!box->isVisible() || support::motionReduced()) return;
    QWidget* host = box->window();
    if (!host) return;
    const QRect r = ctl::indicatorRect(box);
    if (r.width() < 6 || r.height() < 6) return;
    const QPixmap on = ctl::indicatorPixmap(box, r, true);
    const QPixmap off = ctl::indicatorPixmap(box, r, false);
    // The f(x,y) pill hides its tick and carries the state in the chip's fill.
    if (on.toImage() == off.toImage()) return;
    const QRect at(box->mapTo(host, r.topLeft()), r.size());
    DisintegrateOverlay* fx = DisintegrateOverlay::overPixmaps(
        on, off, at, host,
        checked ? DisintegrateOverlay::Sweep::GATHER : DisintegrateOverlay::Sweep::FALL,
        CHECK_SWAP_CELLS, CHECK_SWAP_CELLS, CHECK_SWAP_MS, CHECK_SWAP_SPREAD, CHECK_SWAP_PAD_PX,
        QString::fromLatin1(CHECK_SWAP_OBJECT_NAME));
    if (fx) fx->setProperty(CHECK_SWAP_OWNER_PROPERTY, QVariant::fromValue<QObject*>(box));
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


  QRect ctl::comboFieldRect(const QComboBox* cb) {
    QStyleOptionComboBox o = comboOption(cb, cb->currentText());
    const QRect r =
        cb->style()->subControlRect(QStyle::CC_ComboBox, &o, QStyle::SC_ComboBoxEditField, cb);
    return r.isValid() && !r.isEmpty() ? r : cb->rect();
  }


  // Must run BEFORE hideComboLabel: the stylesheet that blanks the text blanks this too.
  QPixmap ctl::comboLabelPixmap(QComboBox* cb, const QString& text) {
    const qreal dpr = cb->devicePixelRatioF();
    QPixmap pm(QSize(qRound(cb->width() * dpr), qRound(cb->height() * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (cb->isEditable()) {
      // CE_ComboBoxLabel paints nothing for an editable combo (the word is its QLineEdit's).
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


  // A widget stylesheet is the only lever over the app-wide `QComboBox { color: … }`;
  // every state is listed so a hover mid-swap cannot outrank it. Colour only, no reflow.
  void ctl::hideComboLabel(QComboBox* cb, bool hide) {
    if (hide == cb->property(VALUE_SWAP_PROPERTY).toBool()) return;
    if (hide) {
      cb->setProperty(VALUE_SWAP_SHEET_PROPERTY, cb->styleSheet());
      cb->setProperty(VALUE_SWAP_PROPERTY, true);
      const QString p = QString::fromLatin1(VALUE_SWAP_PROPERTY);
      cb->setStyleSheet(cb->property(VALUE_SWAP_SHEET_PROPERTY).toString()
                        + QStringLiteral("QComboBox[%1=\"true\"],QComboBox[%1=\"true\"]:hover,"
                                         "QComboBox[%1=\"true\"]:focus,"
                                         "QComboBox[%1=\"true\"]:on,"
                                         "QComboBox[%1=\"true\"] QLineEdit"
                                         "{color:rgba(0,0,0,0);}")
                              .arg(p));
    } else {
      cb->setProperty(VALUE_SWAP_PROPERTY, false);
      cb->setStyleSheet(cb->property(VALUE_SWAP_SHEET_PROPERTY).toString());
    }
    // Qt matches property selectors at POLISH time.
    repolish(cb);
  }

  ctl::ComboPopupDust::ComboPopupDust(QComboBox* cb) : QObject(cb), cb_(cb) {
    setObjectName(QString::fromLatin1(COMBO_POPUP_FILTER_NAME));
  }

  bool ctl::ComboPopupDust::eventFilter(QObject* o, QEvent* e) {
    if (!cb_) return QObject::eventFilter(o, e);
    auto* popup = qobject_cast<QWidget*>(o);
    if (!popup) return QObject::eventFilter(o, e);
    if (e->type() == QEvent::Show) {
      support::revealPopup(*popup, cb_);
    } else if (e->type() == QEvent::Hide) {
      support::dismissPopup(*popup, cb_);
    }
    return QObject::eventFilter(o, e);
  }


  bool ControlSwapFilter::eventFilter(QObject* o, QEvent* e) {
    const QEvent::Type type = e->type();
    if (type != QEvent::Show && type != QEvent::Polish)
      return QObject::eventFilter(o, e);
    if (o->property(CONTROL_SWAP_WIRED_PROPERTY).toBool()) return QObject::eventFilter(o, e);
    if (auto* box = qobject_cast<QCheckBox*>(o)) {
      box->setProperty(CONTROL_SWAP_WIRED_PROPERTY, true);
      connect(box, &QCheckBox::toggled, box,
              [box](bool on) { swapCheckIndicator(box, on); });
    } else if (auto* cb = qobject_cast<QComboBox*>(o)) {
      cb->setProperty(CONTROL_SWAP_WIRED_PROPERTY, true);
      ctl::rememberComboValue(cb);
      ctl::wireComboPopupDust(cb);
      connect(cb, &QComboBox::currentTextChanged, cb,
              [cb](const QString& to) { ctl::onComboText(cb, to); });
      connect(cb, &QComboBox::textActivated, cb,
              [cb](const QString& to) { ctl::onComboPick(cb, to); });
    }
    return QObject::eventFilter(o, e);
  }


  // Asking for view() is what creates the popup container, so this works at wire time.
  void ctl::wireComboPopupDust(QComboBox* cb) {
    QAbstractItemView* view = cb->view();
    QWidget* popup = view ? view->window() : nullptr;
    if (!popup || popup == cb->window()) return;
    if (cb->findChild<QObject*>(QString::fromLatin1(COMBO_POPUP_FILTER_NAME),
                                Qt::FindDirectChildrenOnly))
      return;
    popup->installEventFilter(new ComboPopupDust(cb));
  }

  void ctl::rememberComboValue(QComboBox* cb) {
    cb->setProperty(VALUE_SWAP_TEXT_PROPERTY, cb->currentText());
    cb->setProperty(VALUE_SWAP_COUNT_PROPERTY, cb->count());
  }
}  // namespace stencil::gui

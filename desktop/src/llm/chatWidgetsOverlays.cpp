#include "chatWidgets.hpp"

namespace stencil::gui {

  // Parented to the thumbnail: the filter dies with the row it belongs to.
  // `minGain` — the preview only opens when it would be meaningfully BIGGER than
  // the thumbnail you are already looking at. A 160px bubble thumbnail next to a
  // 220px popup is just a second copy of the same picture.
  HoverPreview::HoverPreview(QLabel* thumb, QImage full,
                             QString caption) : QObject(thumb), thumb_(thumb), full_(std::move(full)), caption_(std::move(caption)) {
    thumb->setAttribute(Qt::WA_Hover, true);
    thumb->installEventFilter(this);
  }

  bool HoverPreview::eventFilter(QObject* obj, QEvent* event) {
    // While the popup is up the whole app is watched (installed in show):
    // switching window or app never delivers the thumb a Leave, and a ToolTip
    // window otherwise outlives the window it belongs to — still floating over
    // OTHER apps after a Cmd-Tab.
    if (popup_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::WindowDeactivate || t == QEvent::ApplicationDeactivate ||
          t == QEvent::Wheel) {
        // A wheel scrolls the thumb out from UNDER the cursor — no Leave either.
        hide();
      } else if (t == QEvent::MouseMove && thumb_) {
        // Catch-all: the cursor is somewhere the thumb is not (another widget lit
        // its hover while the popup stayed — the missed-Leave family of bugs).
        const QRect r(thumb_->mapToGlobal(QPoint(0, 0)), thumb_->size());
        if (!r.contains(QCursor::pos())) hide();
      } else if ((t == QEvent::KeyPress || t == QEvent::KeyRelease) &&
                 static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt) {
        show();   // re-measure for the Alt state — held doubles the glance
      }
    }
    if (obj != thumb_) return false;
    if (event->type() == QEvent::Enter) show();
    else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) hide();
    return false;   // never consume — the row keeps its own behaviour
  }

  void HoverPreview::hide() {
    if (popup_) popup_->deleteLater();
    popup_ = nullptr;
    qApp->removeEventFilter(this);
  }

  void HoverPreview::show() {
    hide();
    if (full_.isNull() || !thumb_ || !thumb_->isVisible()) return;
    // Already showing it large? Then there is nothing to magnify.
    if (thumb_->width() >= kPreviewEdge * 3 / 4 || thumb_->height() >= kPreviewEdge * 3 / 4) return;
    popup_ = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    popup_->setObjectName(QStringLiteral("chatThumbPreview"));
    popup_->setAttribute(Qt::WA_DeleteOnClose);
    popup_->setAttribute(Qt::WA_ShowWithoutActivating);
    popup_->setAttribute(Qt::WA_StyledBackground);
    // A top-level window inherits no parent styling — paint the card look from the
    // live palette so the preview matches the theme in both light and dark.
    const QColor bg = thumb_->palette().color(QPalette::Window);
    const QColor line = thumb_->palette().color(QPalette::Mid);
    popup_->setStyleSheet(
        QStringLiteral("#chatThumbPreview{background:%1;border:1px solid %2;border-radius:10px;}")
            .arg(bg.name(), line.name()));
    auto* col = new QVBoxLayout(popup_);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(4);
    const QRect avail =
        thumb_->screen() ? thumb_->screen()->availableGeometry() : QRect(0, 0, 1280, 800);
    // Alt HELD doubles the glance (browser .chat-thumb-preview-xl parity) —
    // both the edge and the screen-fraction ceilings, so the doubling shows.
    const int f = (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier) ? 2 : 1;
    const QSize capSize(qMin(kPreviewEdge * f, int(avail.width() * kPreviewScreenW * f)),
                        qMin(kPreviewEdge * f, int(avail.height() * kPreviewScreenH * f)));
    auto* pic = new QLabel(popup_);
    pic->setPixmap(QPixmap::fromImage(
        full_.scaled(capSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    col->addWidget(pic);
    if (!caption_.isEmpty()) {
      auto* cap = makePlainLabel(caption_, popup_);   // a filename — data
      cap->setMaximumWidth(capSize.width());
      cap->setWordWrap(true);
      applyMutedText(cap);
      col->addWidget(cap);
    }
    popup_->adjustSize();
    // BESIDE THE CURSOR, not anchored to the thumbnail: with the dock docked left,
    // a preview placed off the thumbnail's own corner landed way across the window,
    // nowhere near the pointer that asked for it. Flipped whenever the obvious side
    // would run off the screen.
    const QPoint cursor = QCursor::pos();
    const QRect screen = thumb_->screen() ? thumb_->screen()->availableGeometry()
                                          : QRect(cursor - QPoint(400, 300), QSize(800, 600));
    const int w = popup_->width(), h = popup_->height();
    const int gap = 16;
    int x = cursor.x() + gap;
    if (x + w > screen.right() - 8) x = cursor.x() - gap - w;
    int y = cursor.y() - h / 2;
    popup_->move(qBound(screen.left() + 8, x, qMax(screen.left() + 8, screen.right() - w - 8)),
                 qBound(screen.top() + 8, y, qMax(screen.top() + 8, screen.bottom() - h - 8)));
    popup_->show();
    qApp->installEventFilter(this);   // removed in hide() — see eventFilter
  }

  TypingDots::TypingDots(QWidget* parent) : QWidget(parent) {
    setFixedSize(kDotSpan * 3 + kDotGap * 2, kDotSpan + kLift + 2);
    setAccessibleName(QObject::tr("Assistant is answering"));
    connect(&timer_, &QTimer::timeout, this, [this] {
      phase_ = (phase_ + 1) % kSteps;
      update();
    });
    timer_.start(60);
  }

  void TypingDots::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QColor base = palette().color(QPalette::WindowText);
    for (int i = 0; i < 3; ++i) {
      // Each dot runs the same hop, a third of a cycle apart.
      const double t = ((phase_ + i * (kSteps / 3.0)) / kSteps) * 2 * M_PI;
      const double hop = std::max(0.0, std::sin(t));
      QColor c = base;
      c.setAlphaF(0.45 + 0.55 * hop);
      p.setBrush(c);
      p.drawEllipse(QPointF(kDotSpan / 2.0 + i * (kDotSpan + kDotGap),
                            kLift + kDotSpan / 2.0 - hop * kLift),
                    kDotSpan / 2.0, kDotSpan / 2.0);
    }
  }

  ChatCardMore::ChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                             std::function<void()> moved,
                             std::function<QRect()> avoidRect) : QObject(card), card_(card), more_(more), scroll_(scroll), moved_(std::move(moved)), avoidRect_(std::move(avoidRect)) {
    card->installEventFilter(this);
    more->installEventFilter(this);
    if (scroll) {
      // Scrolling moves the content under the viewport, so a button placed
      // against the old visible slice is stale the moment the view moves.
      if (auto* bar = scroll->verticalScrollBar())
        connect(bar, &QScrollBar::valueChanged, this, [this] { if (shown()) place(); });
      scroll->viewport()->installEventFilter(this);
    }
  }

  void ChatCardMore::place() {
    placeChatCardMore(card_, more_, scroll_, avoidRect_ ? avoidRect_() : QRect());
    if (moved_) moved_();
  }

  bool ChatCardMore::eventFilter(QObject* obj, QEvent* event) {
    if (!card_ || !more_) return QObject::eventFilter(obj, event);
    if (obj == card_) {
      switch (event->type()) {
        case QEvent::Enter:
          // show FIRST, then place: placement is the authority on whether the
          // button may appear at all (it hides one that would be clipped out
          // of view or land across a neighbouring row), so showing after it
          // would override its own refusal.
          more_->show();
          place();
          break;
        case QEvent::Leave:
          scheduleHide();   // the button sits across a gap — give it a beat
          break;
        case QEvent::Move:
        case QEvent::Resize:
          if (shown()) place();
          break;
        default:
          break;
      }
    } else if (scroll_ && obj == scroll_->viewport() && event->type() == QEvent::Resize) {
      if (shown()) place();
    } else if (obj == more_) {
      // Any show/hide/move — including the ones the menu and the grace-hide
      // timer cause — re-tells the surface where the button is.
      switch (event->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::Move:
        case QEvent::Resize:
          if (moved_) moved_();
          break;
        default:
          break;
      }
      // Hover lifts it 1px under an accent glow; leaving drops it back.
      if (event->type() == QEvent::Enter && !more_->property("lifted").toBool()) {
        more_->setProperty("lifted", true);
        more_->move(more_->x(), more_->y() - 1);
        if (auto* fx = more_->graphicsEffect()) fx->setEnabled(true);
      } else if (event->type() == QEvent::Leave) {
        if (more_->property("lifted").toBool()) {
          more_->setProperty("lifted", false);
          if (auto* fx = more_->graphicsEffect()) fx->setEnabled(false);
          place();
        }
        scheduleHide();
      }
    }
    return QObject::eventFilter(obj, event);
  }

  void ChatCardMore::scheduleHide() {
    QPointer<ChatCardMore> self(this);
    QTimer::singleShot(220, this, [self] {
      // "chatMenuOpen" is set by showChatCardMenu while this card's menu is
      // up — the button must not vanish from under an open menu.
      if (!self || !self->card_ || !self->more_ ||
          self->card_->property("chatMenuOpen").toBool())
        return;
      const QPoint p = QCursor::pos();
      const auto over = [&p](QWidget* w, int pad) {
        return w->isVisible() &&
               QRect(w->mapToGlobal(QPoint(0, 0)), w->size())
                   .adjusted(-pad, -pad, pad, pad)
                   .contains(p);
      };
      // A padded button rect covers the card→button gap: a slow cursor
      // mid-gap re-arms the check instead of losing the button.
      if (over(self->more_, 14)) { self->scheduleHide(); return; }
      if (!over(self->card_, 0)) self->more_->hide();
    });
  }

}  // namespace stencil::gui

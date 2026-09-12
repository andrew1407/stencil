#include "iconMotion.hpp"

namespace stencil::gui {

  // Which pose set a control uses: `variants.active` on the fullscreen button that is
  // already in fullscreen, so the corners always show where the click takes you.
  const QVector<IconMotionPart>* icm::partsFor(const QAbstractButton* btn,
                                               const IconMotionSpec& spec) {
    const bool active = btn->isChecked()
                        || btn->property(kIconStateProperty).toString()
                               == QLatin1String("active");
    return active && !spec.activeParts.isEmpty() ? &spec.activeParts : &spec.parts;
  }

  bool icm::eligible(QAbstractButton* btn) {
    // A busy spin, a face mid-swap and the fold chevrons each already own their glyph.
    // A DISABLED control is deliberately eligible (iconMotion.json trigger.disabled):
    // it is still hovered and still explains itself through its tooltip, and a frozen
    // glyph read as a dead area of the toolbar rather than as a control that cannot
    // act right now — the motion says what it WOULD do, the grey says it cannot yet.
    if (!btn || support::motionReduced()) return false;
    if (btn->property(kNoIconMotionProperty).toBool()) return false;
    if (faceSwapping(btn)) return false;
    for (QVariantAnimation* a :
         btn->findChildren<QVariantAnimation*>(QStringLiteral("stencilIconSpin")))
      if (a->state() == QAbstractAnimation::Running) return false;
    return true;
  }


  // Q_OBJECT-free (no MOC), so there is no metaobject to qobject_cast through: the
  // runner is found by its unique object name and cast statically — logoHoverFx's
  // asLogoFx() idiom.
  IconMotionRunner* icm::runnerOf(QAbstractButton* btn) {
    QObject* o = btn->findChild<QObject*>(QString::fromLatin1(kIconMotionAnimName),
                                          Qt::FindDirectChildrenOnly);
    return static_cast<IconMotionRunner*>(o);
  }

  ActionIconMotionRunner* icm::runnerOfAction(QAction* act) {
    QObject* o = act->findChild<QObject*>(QString::fromLatin1(kIconMotionAnimName),
                                          Qt::FindDirectChildrenOnly);
    return static_cast<ActionIconMotionRunner*>(o);
  }


  // Which pose set a menu row uses — the action twin of partsFor above.
  const QVector<IconMotionPart>* icm::partsForAction(const QAction* act,
                                                     const IconMotionSpec& spec) {
    const bool active = act->isChecked()
                        || act->property(kIconStateProperty).toString()
                               == QLatin1String("active");
    return active && !spec.activeParts.isEmpty() ? &spec.activeParts : &spec.parts;
  }

  IconMotionFilter::IconMotionFilter(QObject* parent) : QObject(parent) {
    setObjectName(QString::fromLatin1(kIconMotionFilterName));
  }

  bool IconMotionFilter::eventFilter(QObject* o, QEvent* e) {
    const QEvent::Type type = e->type();
    // Keyboard navigation is a hover too (browser .ctx-kb): hovered() fires for the
    // row the arrows land on, where no mouse move ever will. Wired once per menu.
    if (type == QEvent::Show) {
      if (auto* menu = qobject_cast<QMenu*>(o);
          menu && !menu->property(kMenuHoverWiredProperty).toBool()) {
        menu->setProperty(kMenuHoverWiredProperty, true);
        QObject::connect(menu, &QMenu::hovered, this,
                         [this, menu](QAction* a) { hoverMenuAction(menu, a); });
      }
      return QObject::eventFilter(o, e);
    }
    // A menu's rows are QActions inside ONE widget, so their "hover" is the menu's
    // own mouse moves — the browser's .ctx-item / .chat-more-item icons animate on
    // row hover, and these do the same through ActionIconMotionRunner.
    if (type == QEvent::MouseMove || type == QEvent::Leave || type == QEvent::Hide) {
      if (auto* menu = qobject_cast<QMenu*>(o)) {
        if (type == QEvent::MouseMove)
          hoverMenuAction(menu,
                          menu->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint()));
        else if (type == QEvent::Leave)
          hoverMenuAction(menu, nullptr);
        else {  // hidden mid-motion: the row snaps to its rest glyph for the next open
          if (QAction* cur = menuHover_.take(menu))
            if (ActionIconMotionRunner* r = icm::runnerOfAction(cur)) r->rest();
        }
        return QObject::eventFilter(o, e);
      }
    }
    // EnabledChange is deliberately NOT watched: a control greyed out under the pointer
    // keeps its motion, exactly as the browser's CSS trigger does.
    if (type != QEvent::Enter && type != QEvent::Leave && type != QEvent::Hide)
      return QObject::eventFilter(o, e);
    auto* btn = qobject_cast<QAbstractButton*>(o);
    if (!btn) return QObject::eventFilter(o, e);

    if (type == QEvent::Enter) {
      if (!icm::eligible(btn)) return QObject::eventFilter(o, e);
      IconMotionRunner* live = icm::runnerOf(btn);
      IconRequest req;
      // A button carries no glyph name — only the QIcon themedIcon() handed it. A
      // theme flip or a face swap replaces that icon, so the glyph is re-read on every
      // hover and a runner left over from a DIFFERENT glyph is retired, not reused.
      if (!iconRequestForKey(btn->icon().cacheKey(), &req)) {
        if (live) live->enter();   // mid-motion re-enter: the icon is our own posed one
        return QObject::eventFilter(o, e);
      }
      if (live && live->request().name == req.name && live->request().color == req.color
          && live->request().size == req.size) {
        live->enter();
        return QObject::eventFilter(o, e);
      }
      if (live) { live->setObjectName(QString()); live->deleteLater(); }
      const IconMotionSpec* spec = iconMotionFor(req.name);
      if (!spec) return QObject::eventFilter(o, e);
      auto* runner = new IconMotionRunner(btn, req, spec, icm::partsFor(btn, *spec));
      runner->enter();
    } else if (IconMotionRunner* live = icm::runnerOf(btn)) {
      if (type == QEvent::Leave) live->leave();
      else live->rest();   // hidden mid-motion: back to the rest pose
    }
    return QObject::eventFilter(o, e);
  }


  // The row the pointer is on, per menu — so moving to the next row eases the
  // previous glyph back exactly as leaving a button does.
  void IconMotionFilter::hoverMenuAction(QMenu* menu, QAction* a) {
    QPointer<QAction>& cur = menuHover_[menu];
    if (cur == a) return;
    if (cur)
      if (ActionIconMotionRunner* r = icm::runnerOfAction(cur)) r->leave();
    cur = a;
    if (!a || a->isSeparator() || support::motionReduced()) return;
    if (a->property(kNoIconMotionProperty).toBool()) return;
    IconRequest req;
    // Same re-trace as the button path: a theme flip replaces the QIcon, so the
    // glyph is re-read on every hover and a stale runner retired, not reused.
    if (!iconRequestForKey(a->icon().cacheKey(), &req)) {
      if (ActionIconMotionRunner* live = icm::runnerOfAction(a)) live->enter();
      return;
    }
    if (ActionIconMotionRunner* live = icm::runnerOfAction(a)) {
      if (live->request().name == req.name && live->request().color == req.color
          && live->request().size == req.size) {
        live->enter();
        return;
      }
      live->setObjectName(QString());
      live->deleteLater();
    }
    const IconMotionSpec* spec = iconMotionFor(req.name);
    if (!spec) return;
    auto* runner = new ActionIconMotionRunner(a, req, spec, icm::partsForAction(a, *spec));
    runner->enter();
  }


  // Install the watcher on the application. Idempotent — every MainWindow calls it, and
  // only the first one takes.
  void installIconMotion() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app
        || app->findChild<QObject*>(QString::fromLatin1(kIconMotionFilterName),
                                    Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new IconMotionFilter(app));
  }
}  // namespace stencil::gui

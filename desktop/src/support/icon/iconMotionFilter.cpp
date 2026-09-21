#include "iconMotion.hpp"

namespace stencil::gui {

  // `variants.active` on the fullscreen button already in fullscreen.
  const QVector<IconMotionPart>* icm::partsFor(const QAbstractButton* btn,
                                               const IconMotionSpec& spec) {
    const bool active = btn->isChecked()
                        || btn->property(ICON_STATE_PROPERTY).toString()
                               == QLatin1String("active");
    return active && !spec.activeParts.isEmpty() ? &spec.activeParts : &spec.parts;
  }

  bool icm::eligible(QAbstractButton* btn) {
    // A DISABLED control is deliberately eligible (iconMotion.json trigger.disabled): the
    // motion says what it WOULD do, the grey says it cannot yet.
    if (!btn || support::motionReduced()) return false;
    if (btn->property(NO_ICON_MOTION_PROPERTY).toBool()) return false;
    if (faceSwapping(btn)) return false;
    for (QVariantAnimation* a :
         btn->findChildren<QVariantAnimation*>(QStringLiteral("stencilIconSpin")))
      if (a->state() == QAbstractAnimation::Running) return false;
    return true;
  }


  // No MOC: found by unique object name and cast statically (logoHoverFx's asLogoFx idiom).
  IconMotionRunner* icm::runnerOf(QAbstractButton* btn) {
    QObject* o = btn->findChild<QObject*>(QString::fromLatin1(ICON_MOTION_ANIM_NAME),
                                          Qt::FindDirectChildrenOnly);
    return static_cast<IconMotionRunner*>(o);
  }

  ActionIconMotionRunner* icm::runnerOfAction(QAction* act) {
    QObject* o = act->findChild<QObject*>(QString::fromLatin1(ICON_MOTION_ANIM_NAME),
                                          Qt::FindDirectChildrenOnly);
    return static_cast<ActionIconMotionRunner*>(o);
  }


  const QVector<IconMotionPart>* icm::partsForAction(const QAction* act,
                                                     const IconMotionSpec& spec) {
    const bool active = act->isChecked()
                        || act->property(ICON_STATE_PROPERTY).toString()
                               == QLatin1String("active");
    return active && !spec.activeParts.isEmpty() ? &spec.activeParts : &spec.parts;
  }

  IconMotionFilter::IconMotionFilter(QObject* parent) : QObject(parent) {
    setObjectName(QString::fromLatin1(ICON_MOTION_FILTER_NAME));
  }

  bool IconMotionFilter::eventFilter(QObject* o, QEvent* e) {
    const QEvent::Type type = e->type();
    // Keyboard navigation is a hover too (browser .ctx-kb): hovered() fires for the arrowed row.
    if (type == QEvent::Show) {
      if (auto* menu = qobject_cast<QMenu*>(o);
          menu && !menu->property(MENU_HOVER_WIRED_PROPERTY).toBool()) {
        menu->setProperty(MENU_HOVER_WIRED_PROPERTY, true);
        QObject::connect(menu, &QMenu::hovered, this,
                         [this, menu](QAction* a) { hoverMenuAction(menu, a); });
      }
      return QObject::eventFilter(o, e);
    }
    // Browser .ctx-item / .chat-more-item icons animate on row hover.
    if (type == QEvent::MouseMove || type == QEvent::Leave || type == QEvent::Hide) {
      if (auto* menu = qobject_cast<QMenu*>(o)) {
        if (type == QEvent::MouseMove)
          hoverMenuAction(menu,
                          menu->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint()));
        else if (type == QEvent::Leave)
          hoverMenuAction(menu, nullptr);
        else {  // hidden mid-motion: the row snaps to its rest glyph for the next open
          if (QAction* cur = menuHover.take(menu))
            if (ActionIconMotionRunner* r = icm::runnerOfAction(cur)) r->rest();
        }
        return QObject::eventFilter(o, e);
      }
    }
    // EnabledChange is NOT watched: a control greyed out under the pointer keeps its motion.
    if (type != QEvent::Enter && type != QEvent::Leave && type != QEvent::Hide)
      return QObject::eventFilter(o, e);
    auto* btn = qobject_cast<QAbstractButton*>(o);
    if (!btn) return QObject::eventFilter(o, e);

    if (type == QEvent::Enter) {
      if (!icm::eligible(btn)) return QObject::eventFilter(o, e);
      IconMotionRunner* live = icm::runnerOf(btn);
      IconRequest req;
      // A theme flip or face swap replaces the QIcon, so a runner for a DIFFERENT glyph is retired.
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


  void IconMotionFilter::hoverMenuAction(QMenu* menu, QAction* a) {
    // hovered() is emitted along the whole caused stack, so a SUBMENU's row arrives here for its
    // parent too. Each level owns only its own rows, so a row is entered once, not once per level.
    if (a && !menu->actionGeometry(a).isValid()) return;
    QPointer<QAction>& cur = menuHover[menu];
    if (cur == a) return;
    if (cur)
      if (ActionIconMotionRunner* r = icm::runnerOfAction(cur)) r->leave();
    cur = a;
    if (!a || a->isSeparator() || support::motionReduced()) return;
    if (a->property(NO_ICON_MOTION_PROPERTY).toBool()) return;
    IconRequest req;
    // Same re-trace as the button path.
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


  // Idempotent — every MainWindow calls it; only the first takes.
  void installIconMotion() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app
        || app->findChild<QObject*>(QString::fromLatin1(ICON_MOTION_FILTER_NAME),
                                    Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new IconMotionFilter(app));
  }
}  // namespace stencil::gui

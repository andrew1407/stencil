#include "altPeek.hpp"

#include <QList>
#include <QPointer>
#include <QWidget>

namespace stencil::support {

  namespace {
    struct Handle {
      QPointer<QObject> owner;
      GlideCloser close;
    };
    QList<Handle>& registry() {
      static QList<Handle> handles;
      return handles;
    }
  }  // namespace

  void addGlideHandle(QObject* owner, GlideCloser close) {
    if (!owner) return;
    registry().append({owner, std::move(close)});
    QObject::connect(owner, &QObject::destroyed, [owner] {
      registry().removeIf([owner](const Handle& h) { return h.owner.isNull() || h.owner == owner; });
    });
  }

  void glideFrom(const QObject* self, QWidget* opener) {
    const QList<Handle> handles = registry();   // a closer may add or drop handles
    for (const Handle& h : handles)
      if (h.owner && h.owner != self) h.close(opener);
  }

  AltPeekGesture::AltPeekGesture(Deps deps, QObject* parent)
      : QObject(parent), deps(std::move(deps)) {
    linger.setSingleShot(true);
    linger.setInterval(LINGER_CLOSE_MS);
    connect(&linger, &QTimer::timeout, this, [this] {
      if (state == Mode::LINGER) closeOwn();
    });
    addGlideHandle(this, [this](QWidget*) {
      if (state != Mode::NONE) closeOwn();
    });
  }

  void AltPeekGesture::altHover(QWidget* opener) {
    if (deps.isOpen()) return;
    glideFrom(this, opener);
    state = Mode::PEEK;
    deps.open();
  }

  void AltPeekGesture::altRelease() {
    if (state != Mode::PEEK) return;
    if (deps.isEngaged()) {
      state = Mode::LINGER;
      return;
    }
    closeOwn();
  }

  void AltPeekGesture::boxEnter() { linger.stop(); }

  void AltPeekGesture::boxLeave() {
    if (state != Mode::LINGER || !deps.isOpen()) return;
    linger.start();
  }

  void AltPeekGesture::notifyClosed() {
    state = Mode::NONE;
    linger.stop();
  }

  void AltPeekGesture::closeOwn() {
    state = Mode::NONE;
    linger.stop();
    if (deps.isOpen()) deps.close();
  }

}  // namespace stencil::support

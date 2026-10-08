// The drag-time ⋯'s arrival and leaving: in a particle mode it forms out of its dust, veiled until
// the cloud lands, and leaves into it; otherwise the controls' slot slide, or a plain show/hide.
// Browser twin: ui/projects/list/dragMenu.js (surfaceIn / surfaceOut over css/animations/dust.css).
#include "ProjectDragMenu.hpp"
#include "DisintegrateOverlay.hpp"
#include "controlReveal.hpp"
#include "menuReveal.hpp"
#include "motionPrefs.hpp"

#include <QFile>
#include <QGraphicsOpacityEffect>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QMenu>   // the header's QPointer<QMenu> needs the type here
#include <QPropertyAnimation>
#include <QToolButton>

namespace stencil::gui {

  namespace {
    // ms: motion.json's dust SURFACE_MENU_IN_MS / SURFACE_MENU_OUT_MS, the browser ⋯'s clocks.
    int surfaceMs(const char* key, int fallback) {
      static const QJsonObject dust = [] {
        QFile f(QStringLiteral(":/config/motion.json"));
        if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
        return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("dust")).toObject();
      }();
      const int ms = dust.value(QLatin1String(key)).toInt(0);
      return ms > 0 ? ms : fallback;
    }
  }  // namespace

  int ProjectDragMenu::moreInMs() { return surfaceMs("SURFACE_MENU_IN_MS", 420); }
  int ProjectDragMenu::moreOutMs() { return surfaceMs("SURFACE_MENU_OUT_MS", 270); }

  void ProjectDragMenu::showMore(bool show) {
    if (entrance) more->setGraphicsEffect(nullptr);   // the veil owns its fade: both go
    // A child's veil is a graphics effect, not a window's opacity: offscreen plays it too.
    if (!support::isDustAllowed()) {
      revealControls(more, show);
      return;
    }
    if (!show) {
      // The cloud starts as an exact copy of the chip, so the chip can go at once (surfaceLeave).
      if (more->isVisible())
        flyTipDust(more, more->window(), more->mapToGlobal(more->rect().center()), false, moreOutMs(), false);
      more->hide();
      return;
    }
    more->show();   // no paint before the veil below: both land in this turn
    if (QLayout* row = more->parentWidget() ? more->parentWidget()->layout() : nullptr) row->activate();
    if (!flyTipDust(more, more->window(), more->mapToGlobal(more->rect().center()), true, moreInMs(), false))
      return;
    QGraphicsOpacityEffect* veil = veilBehindDust(more);
    // Hidden while the motes gather, then up over the last beat (surfaceForm: 0 to 55%, then 1).
    auto* fade = new QPropertyAnimation(veil, "opacity", veil);
    holdFadeKeys(fade, moreInMs());
    QToolButton* chip = more;
    connect(fade, &QAbstractAnimation::finished, chip, [chip, veil] {
      if (chip->graphicsEffect() == veil) chip->setGraphicsEffect(nullptr);
    });
    entrance = fade;
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui

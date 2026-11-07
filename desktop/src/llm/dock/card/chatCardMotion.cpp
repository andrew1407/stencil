#include "chatCardMotion.hpp"

#include "../chatDockShared.hpp"
#include "../../panel/chatWidgets.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/genie/GenieGhost.hpp"
#include "../../../support/genie/ghostLabel.hpp"
#include "../../../support/menu/popupSlide.hpp"   // POPUP_GENIE_MS
#include "../../../support/motionPrefs.hpp"

#include <QEasingCurve>
#include <QScrollArea>
#include <QGraphicsOpacityEffect>
#include <QLabel>

namespace stencil::gui {

  namespace {

    // The picture shrinks into the bottom corner on the card's side, fading as it goes (chatGrowIn reversed).
    void shrinkIntoTail(QWidget* view, const QPixmap& shot, const QRect& box, bool isRight) {
      QLabel* ghost = support::genie::makeGhostLabel(view, shot);
      auto* fx = new QGraphicsOpacityEffect(ghost);
      ghost->setGraphicsEffect(fx);
      support::genie::flyGhostLabel(ghost, support::motionMs(chatShrinkMs()), QEasingCurve::InCubic,
                                    [fx, box, isRight](QLabel& g, double t) {
        g.setGeometry(support::genie::tailBox(box, 1.0 - 0.8 * t, isRight));
        fx->setOpacity(t < 0.7 ? 1.0 : (1.0 - t) / 0.3);
      });
    }
  }  // namespace

  bool chatCardLeave(QWidget* card, QScrollArea* scroll) {
    const bool isSlide = support::isSlideMotionOk();
    if (!card || !scroll || !(isSlide || support::isTransitionMotionOk()) || !card->isVisible()) return false;
    QWidget* view = scroll->viewport();
    const QRect at(card->mapToGlobal(QPoint(0, 0)), card->size());
    const QRect seen(view->mapToGlobal(QPoint(0, 0)), view->size());
    if (!at.intersects(seen)) return false;
    const QPixmap shot = chatCardShot(card);
    const bool isRight = isOnRightSide(seen, at);
    if (isSlide) {
      if (!support::genie::fly(view, shot, at, chatTailHome(seen, at), /*isOpening=*/false, /*asWindow=*/false, {},
                               support::POPUP_GENIE_MS))
        return false;
    } else {
      shrinkIntoTail(view, shot, QRect(view->mapFromGlobal(at.topLeft()), at.size()), isRight);
    }
    card->hide();
    return true;
  }

  int chatClearWaitMs() {
    if (support::isDustAllowed()) return DisintegrateOverlay::ITEM_MS + 60;
    if (support::isSlideMotionOk()) return support::POPUP_GENIE_MS;
    if (support::isTransitionMotionOk()) return chatShrinkMs();
    return chatdock::CHAT_LEAVE_MS;
  }

}  // namespace stencil::gui

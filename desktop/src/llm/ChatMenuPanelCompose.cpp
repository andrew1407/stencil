// The flyout composer's action row. Browser twin js/ui/chatComposer.js: Send stays
// inline and everything else lives behind the "…", which also carries the status dot.
#include "ChatMenuPanel.hpp"
#include "chatMenuPanelParts.hpp"

#include "chatWidgets.hpp"              // makeChatAccentButton
#include "../support/MenuShimmer.hpp"   // the shared per-row hover sweep
#include "../support/guiHelpers.hpp"    // compactIconMenu / fitMenuWidth
#include "../support/menuReveal.hpp"    // the menu grows out of its trigger

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QRect>
#include <QToolButton>

namespace stencil::gui {

  void ChatMenuPanel::buildComposerActions(QWidget* host, QHBoxLayout* btnRow) {
    const auto mkBtn = [host](const char* name, const QString& tip) {
      QToolButton* b = makeChatAccentButton(host, tip);
      b->setObjectName(QString::fromLatin1(name));
      return b;
    };
    send_ = mkBtn("chatMenuSend", QString());
    QObject::connect(send_, &QToolButton::clicked, this, [this] {
      if (busy_) {
        if (onStop_) onStop_();
        return;
      }
      submit();
    });
    btnRow->addWidget(send_);

    more_ = mkBtn("chatMenuMore", QString());
    more_->setAccessibleName(QStringLiteral("More actions"));
    auto* menu = new QMenu(more_);
    menu->setObjectName(QStringLiteral("chatMenuMoreMenu"));
    // The DOCK's rows, in its order and with its glyphs — Clear history is the dock's alone.
    moreRows_ = buildChatMoreMenu(*menu, /*withClear=*/false);
    moreRows_.attach->setObjectName(QStringLiteral("chatMenuAddImage"));
    moreRows_.settings->setObjectName(QStringLiteral("chatMenuSettings"));
    QObject::connect(moreRows_.attach, &QAction::triggered, this, [this] {
      if (onAttach_) onAttach_();  // the picker needs the popup chain gone first
    });
    // The same preference the dock's row drives; the owner persists it and mirrors it back.
    QObject::connect(moreRows_.swapSides, &QAction::triggered, this, [this] {
      setChatSwapSides(!chatSwapSides_);
      emit chatSwapSidesChanged(chatSwapSides_);
    });
    QObject::connect(moreRows_.settings, &QAction::triggered, this, [this] {
      // Captured NOW: the settings dialog opens after this popup closes (a modal
      // fights the popup's own grab), which would hide the trigger first.
      if (onSettings_)
        onSettings_(QRect(more_->mapToGlobal(QPoint(0, 0)), more_->size()));
    });
    // An item that cannot act right now HIDES rather than greys out (browser parity).
    QObject::connect(menu, &QMenu::aboutToShow, this, [this, menu] {
      moreRows_.attach->setVisible(!busy_);
      fitMenuWidth(*menu);
    });
    compactIconMenu(*menu);
    new support::MenuShimmer(menu, menu);
    support::revealMenuFrom(*menu, more_);
    // Popped from clicked(), not setPopupMode: the hosting StayOpenMenu re-dispatches a
    // click, never the press an InstantPopup listens for.
    QObject::connect(more_, &QToolButton::clicked, this, [this, menu] {
      menu->popup(more_->mapToGlobal(QPoint(0, more_->height())));
    });
    btnRow->addWidget(more_);

    // Reachability dot riding on the "…" corner — the dock's badge, same geometry,
    // fed by the same refreshLlmStatus probe.
    statusDot_ = new QLabel(more_);
    statusDot_->setObjectName(QStringLiteral("chatMenuStatusDot"));
    statusDot_->setFixedSize(7, 7);
    statusDot_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusDot_->move(MENU_CHAT_BUTTON_EDGE - statusDot_->width() - 1, 1);
    statusDot_->raise();
  }

}  // namespace stencil::gui

// The flyout composer's action row. Browser twin js/ui/chatComposer.js: Send stays
// inline and everything else lives behind the "…", which also carries the status dot.
#include "ChatMenuPanel.hpp"
#include "chatMenuPanelParts.hpp"

#include "chatWidgets.hpp"              // makeChatAccentButton
#include "../../support/motion/MenuShimmer.hpp"   // the shared per-row hover sweep
#include "../../support/guiHelpers.hpp"    // compactIconMenu / fitMenuWidth
#include "../../support/menu/menuReveal.hpp"    // the menu grows out of its trigger

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
    send = mkBtn("chatMenuSend", QString());
    QObject::connect(send, &QToolButton::clicked, this, [this] {
      if (busy) {
        if (onStop) onStop();
        return;
      }
      submit();
    });
    btnRow->addWidget(send);

    more = mkBtn("chatMenuMore", QString());
    more->setAccessibleName(QStringLiteral("More actions"));
    auto* menu = new QMenu(more);
    menu->setObjectName(QStringLiteral("chatMenuMoreMenu"));
    // The DOCK's rows, in its order and with its glyphs — Clear history is the dock's alone.
    moreRows = buildChatMoreMenu(*menu, /*withClear=*/false);
    moreRows.attach->setObjectName(QStringLiteral("chatMenuAddImage"));
    moreRows.settings->setObjectName(QStringLiteral("chatMenuSettings"));
    QObject::connect(moreRows.attach, &QAction::triggered, this, [this] {
      if (onAttach) onAttach();  // the picker needs the popup chain gone first
    });
    // The same preference the dock's row drives; the owner persists it and mirrors it back.
    QObject::connect(moreRows.swapSides, &QAction::triggered, this, [this] {
      setChatSwapSides(!chatSwapSides);
      emit chatSwapSidesChanged(chatSwapSides);
    });
    QObject::connect(moreRows.settings, &QAction::triggered, this, [this] {
      // Captured NOW: the settings dialog opens after this popup closes (a modal
      // fights the popup's own grab), which would hide the trigger first.
      if (onSettings)
        onSettings(QRect(more->mapToGlobal(QPoint(0, 0)), more->size()));
    });
    // An item that cannot act right now HIDES rather than greys out (browser parity).
    QObject::connect(menu, &QMenu::aboutToShow, this, [this, menu] {
      moreRows.attach->setVisible(!busy);
      fitMenuWidth(*menu);
    });
    compactIconMenu(*menu);
    new support::MenuShimmer(menu, menu);
    support::revealMenuFrom(*menu, more);
    // Popped from clicked(), not setPopupMode: the hosting StayOpenMenu re-dispatches a
    // click, never the press an InstantPopup listens for.
    QObject::connect(more, &QToolButton::clicked, this, [this, menu] {
      menu->popup(more->mapToGlobal(QPoint(0, more->height())));
    });
    btnRow->addWidget(more);

    // Reachability dot riding on the "…" corner — the dock's badge, same geometry,
    // fed by the same refreshLlmStatus probe.
    statusDot = new QLabel(more);
    statusDot->setObjectName(QStringLiteral("chatMenuStatusDot"));
    statusDot->setFixedSize(7, 7);
    statusDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusDot->move(MENU_CHAT_BUTTON_EDGE - statusDot->width() - 1, 1);
    statusDot->raise();
  }

}  // namespace stencil::gui

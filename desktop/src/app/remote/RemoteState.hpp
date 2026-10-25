#pragma once
#include "RemoteSession.hpp"

namespace stencil::net { class ConnectionManager; }

namespace stencil::gui {

  // The window's side of the Stencil servers: the saved connections (built on first use), the live
  // co-edit session, and the reentrancy flags RemoteSyncController reads as const bool*.
  struct RemoteState {
    RemoteSession* session = nullptr;
    // The saved connections, null until first used (RemoteSession::ensureConnections).
    stencil::net::ConnectionManager* conns() const { return session ? session->getConnections() : nullptr; }
    bool pushing = false;
    int pushSeq = 0;     // the push that owns `pushing`, as reloadSeq owns `reloading`
    bool reloading = false;
    int reloadSeq = 0;   // the reload that owns `reloading`; an older one's late clear leaves it alone
  };

}  // namespace stencil::gui

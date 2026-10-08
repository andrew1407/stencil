#pragma once

namespace stencil::net { class ConnectionManager; }

namespace stencil::gui {

  class RemoteSession;

  // The window's side of the Stencil servers: the saved connections (built on first use), the live
  // co-edit session, and the reentrancy flags RemoteSyncController reads as const bool*.
  struct RemoteState {
    stencil::net::ConnectionManager* connections = nullptr;
    RemoteSession* session = nullptr;
    bool pushing = false;
    bool reloading = false;
    int reloadSeq = 0;   // the reload that owns `reloading`; an older one's late clear leaves it alone
  };

}  // namespace stencil::gui

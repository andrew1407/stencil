#pragma once
// The headless stand-in for a synchronous connect. ConnectionManager only hands out
// async handshakes now (net/serverClient.hpp: a nested event loop inside the client
// re-entered paint and input), so a test that wants one drives the loop itself — which
// is exactly what a test may do and a GUI may not.
#include <QCoreApplication>
#include <QDeadlineTimer>
#include "serverClient.hpp"

namespace stencil::test {

  // Connect and wait. `err` gets the failure reason, as the old connectTo did.
  inline bool connectNow(
      stencil::net::ConnectionManager& mgr, const QString& url, const QString& token,
      QString& err,
      stencil::net::ServerClient::CredentialKind kind =
          stencil::net::ServerClient::CredentialKind::NONE,
      int timeoutMs = 10000) {
    bool done = false, ok = false;
    mgr.connectToAsync(url, token, [&](bool o, QString e) {
      ok = o;
      err = std::move(e);
      done = true;
    }, kind);
    QDeadlineTimer deadline(timeoutMs);
    while (!done && !deadline.hasExpired())
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return ok;
  }

  // …and the same for signing an already-listed connection back in.
  inline bool reauthNow(stencil::net::ConnectionManager& mgr, const QString& url,
                        const QString& token, QString& err, int timeoutMs = 10000) {
    bool done = false, ok = false;
    mgr.reauthenticateAsync(url, token, [&](bool o, QString e) {
      ok = o;
      err = std::move(e);
      done = true;
    });
    QDeadlineTimer deadline(timeoutMs);
    while (!done && !deadline.hasExpired())
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return ok;
  }

}  // namespace stencil::test

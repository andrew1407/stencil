// Headless check of STALE-SESSION handling (net/serverClient + the Servers dialog
// row), mirroring the browser's design:
//   - a credential the server REFUSES (401/403) is its own status — Expired — kept
//     apart from Error, which means "unreachable"; the saved connection survives;
//   - an expired client is never retried in a loop: further calls leave it expired
//     and issue no fresh auth of their own;
//   - an admin token still works (the client mints a session with it);
//   - a valid token still connects normally;
//   - the dialog row for an expired connection wears the amber card and a LABELLED
//     amber Reconnect (browser .connect-expired: the card says what is wrong, so there
//     is no separate note), and signing in again turns it green.
// …plus the credential KIND that rides along with it (browser credentialKind parity):
// Admin when the credential proved it can mint a session, Session when the token passed
// the /projects probe, None otherwise — persisted with the saved connections and shown as
// the row's golden band, its Invite button, and the All / Admin / Non-admin filter, whose
// changes play as a question re-answered (support/filterFade).
// A mock QTcpServer stands in for the collaboration server, so no Go server is needed.
#include "serverAuthParts.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("serverAuthHeadless");

  serverauth::MockServer mock;
  check(mock.listen(), "mock server listens");
  serverauth::MockServer mock2;   // a SECOND origin, so one manager can hold two kinds of row
  check(mock2.listen(), "second mock server listens");

  serverauth::checkClassification(mock, mock2);
  serverauth::checkCredentialKind(mock, mock2);
  serverauth::checkRemintAndExpiry(mock, mock2);
  serverauth::checkInvites(mock, mock2);
  serverauth::checkAdminRow(mock, mock2);

  std::printf(failures ? "FAILURE (%d failures)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
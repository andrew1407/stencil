// Headless check of STALE-SESSION handling (net/serverClient + the Servers dialog row), mirroring the
// browser: a credential the server REFUSES (401/403) is its own Expired status, kept apart from Error's
// "unreachable" and never retried in a loop, while an admin or a valid token still connects, and the
// expired row wears the amber card with a LABELLED amber Reconnect. Plus the credential KIND riding
// along — Admin / Session / None, persisted and shown as the golden band, Invite and the filter.
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
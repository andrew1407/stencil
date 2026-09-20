#pragma once
// The two HTTP outcomes every desktop caller triages on, named once so the REST client and
// the LLM client agree. Port of the browser's `resp.ok` and net/urlRules.js isAuthStatus.
namespace stencil::net {

  inline constexpr bool isOkStatus(int status) { return status >= 200 && status < 300; }

  // A refused credential, not a broken server: the saved row is kept for re-sign-in.
  inline constexpr bool isAuthStatus(int status) { return status == 401 || status == 403; }

}  // namespace stencil::net

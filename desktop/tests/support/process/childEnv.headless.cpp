// The environment a desktop child process starts with (support/process/childEnv.hpp): the
// credentials cli/src/safety/child.zig scrubs are gone, everything else is the parent's.
#include "../../../src/support/process/childEnv.hpp"

#include <cstdio>

#include "../../support/check.hpp"

int main() {
  QProcessEnvironment env;
  env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin"));
  env.insert(QStringLiteral("STENCIL_LLM_API_KEY"), QStringLiteral("sk-secret"));
  env.insert(QStringLiteral("STENCIL_LLM_SERVER_TOKEN"), QStringLiteral("tok"));
  env.insert(QStringLiteral("stencil_llm_base_url"), QStringLiteral("http://localhost:1234/v1"));
  env.insert(QStringLiteral("STENCIL_SERVER_TOKEN"), QStringLiteral("srv"));
  env.insert(QStringLiteral("STENCIL_SERVER_TOKENS"), QStringLiteral("http://h=srv"));
  env.insert(QStringLiteral("STENCIL_SERVER_URL"), QStringLiteral("http://h"));
  const QProcessEnvironment out = stencil::support::scrubbedChildEnv(env);
  check(out.value(QStringLiteral("PATH")) == QStringLiteral("/usr/bin"), "the rest is inherited");
  check(out.contains(QStringLiteral("STENCIL_SERVER_URL")), "a server address is not a credential");
  for (const char* gone : {"STENCIL_LLM_API_KEY", "STENCIL_LLM_SERVER_TOKEN", "stencil_llm_base_url",
                           "STENCIL_SERVER_TOKEN", "STENCIL_SERVER_TOKENS"})
    check(!out.contains(QString::fromLatin1(gone)), gone);
  check(out.keys().size() == 2, "only the two plain keys are left");

  qputenv("STENCIL_LLM_API_KEY", "sk-live");
  check(!stencil::support::scrubbedChildEnv().contains(QStringLiteral("STENCIL_LLM_API_KEY")),
        "the default starts from this process's own environment, scrubbed");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

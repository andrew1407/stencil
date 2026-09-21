// Headless check of the name display-shortening (src/support/displayName.hpp) — the
// desktop port of browser/js/utils.js shortName. Mirrors browser/tests/shortName.test.js
// and browser-extension/tests/displayName.test.js case for case: all three must agree, or the
// same project reads with a different name in each app. Pure QtCore; no display needed.
#include "displayName.hpp"

#include <QCoreApplication>
#include <cstdio>

using stencil::support::NAME_DISPLAY_CHARS;
using stencil::support::shortName;

#include "../support/check.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // The shared limit — the three ports must agree on it.
  check(NAME_DISPLAY_CHARS == 28, "limit matches the browser/extension helpers");

  // At or under the limit: untouched.
  check(shortName("portrait") == QStringLiteral("portrait"), "short name untouched");
  const QString exact(NAME_DISPLAY_CHARS, QLatin1Char('x'));
  check(shortName(exact) == exact, "exactly at the limit is not shortened");

  // The reported case: a 53-char Amazon CDN slug.
  const QString slug = QStringLiteral("MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_");
  const QString out = shortName(slug);
  check(out.size() == NAME_DISPLAY_CHARS, "shortened to exactly the limit");
  check(out == QStringLiteral("MV5BODg3MzYwMj…NzAzNw@@._V1_"),
        "same output as the browser/extension ports");

  // The tail is the whole point of a MIDDLE ellipsis: it keeps two names that differ
  // only at the end distinguishable.
  check(shortName(slug + ".jpg") != shortName(slug + "-copy.jpg"),
        "names differing only at the end stay distinct");

  // Odd remainder → the extra char goes to the head (limit 8 → keep 7 → head 4, tail 3).
  check(shortName(QStringLiteral("abcdefghijklmnop"), 8) == QStringLiteral("abcd…nop"),
        "odd remainder splits head-heavy");
  // Even remainder splits evenly (limit 5 → keep 4 → head 2, tail 2).
  check(shortName(QStringLiteral("abcdefghij"), 5) == QStringLiteral("ab…ij"),
        "even remainder splits evenly");

  // Degrade safely rather than slicing with a negative length.
  check(shortName(QStringLiteral("abcdef"), 1) == QStringLiteral("…"), "limit 1 → ellipsis only");
  check(shortName(QStringLiteral("abcdef"), 2) == QStringLiteral("a…"), "limit 2 → head only");
  check(shortName(QString()) == QString(), "empty name stays empty");

  std::printf("%s\n", failures == 0 ? "displayName: all checks passed"
                                    : "displayName: FAILURES");
  return failures == 0 ? 0 : 1;
}

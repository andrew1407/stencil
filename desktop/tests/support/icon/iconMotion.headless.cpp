// Headless check of the per-icon hover motion (src/support/iconMotion.hpp) — the desktop port of
// browser/js/config/iconMotion.json, where every glyph mimes its OWN action, split across
// iconMotion*.headless.cpp. This TU owns the table the sections read and the `--dump <dir>` PNGs.
#include "iconMotionParts.hpp"

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QString dumpDir;
  for (int i = 1; i < argc - 1; ++i)
    if (QLatin1String(argv[i]) == QLatin1String("--dump")) dumpDir = QString::fromLocal8Bit(argv[i + 1]);

  const QHash<QString, IconMotionSpec>& table = icm::table();
  tableAgainstTheCanon(table);
  glyphMeanings();
  driverOnALiveButton();

  // ── Optional visual dump ────────────────────────────────────────────────────
  if (!dumpDir.isEmpty()) {
    QDir().mkpath(dumpDir);
    for (auto it = table.begin(); it != table.end(); ++it) {
      const IconMotionSpec& s = it.value();
      // Enough stops to read a fast ease: an OutBack rise is most of the way home by 35%.
      for (double f : {0.0, 0.08, 0.15, 0.25, 0.4, 0.6, 0.8, 1.0})
        frame(it.key(), s, s.totalMs * f)
            .save(QStringLiteral("%1/%2@%3.png").arg(dumpDir, it.key()).arg(int(f * 100), 3, 10,
                                                                            QLatin1Char('0')));
    }
    for (double f : {0.0, 1.0}) {
      const IconMotionSpec* mx = iconMotionFor(QStringLiteral("maximize"));
      frame("maximize", *mx, mx->activeParts, mx->totalMs * f)
          .save(QStringLiteral("%1/maximize-active@%2.png").arg(dumpDir).arg(int(f * 100)));
    }
    std::printf("  dumped frames to %s\n", qPrintable(dumpDir));
  }

  // The idle "＋ Blank image" card mirrors the `image` entry by hand: its glyph is stroked by the canvas
  // itself, out of the app-wide hover watcher's reach, so its constants must BE the canon's numbers.
  std::printf("idle card glyph:\n");
  idleCardGlyph();

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}

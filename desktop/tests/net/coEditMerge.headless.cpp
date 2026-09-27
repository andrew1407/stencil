// The co-edit smoke suite's second section: A's guarded save, B's reload seeing it, B's stale save
// merging instead of clobbering, and both converging on one layout; then the project goes.
#include "coEditParts.hpp"

namespace coedit {

  void saveAndConverge(ServerClient* A, ServerClient* B, const QString& id, qint64 v0, qint64 bVersion) {
    // ── Editor A: save an annotation [A1] (saveToServer's guarded-write path) ──
    qint64 vA = 0;
    {
      auto ready = std::make_shared<bool>(false);
      auto linesA = std::make_shared<QJsonArray>();
      linesA->append(line("A1"));
      guardedSave(A, id, v0, linesA, [&](bool committed, qint64 nv) {
        check(committed, "A: guarded save committed");
        vA = nv;
        *ready = true;
      });
      pump(ready);
    }
    check(vA > v0, "A: server version advanced after the save");

    // ── Editor B: poll/reload (openServerProject silent-reload path) sees A's change ──
    {
      auto ready = std::make_shared<bool>(false);
      B->getProjectAsync(id, [&](bool ok, ServerProject meta, QJsonObject layout) {
        check(ok && meta.version == vA, "B: poll sees the new version (peer edit visible)");
        check(layoutHas(layout, "A1"), "B: peer's annotation [A1] is present after reload");
        *ready = true;
      });
      pump(ready);
    }

    // Editor B: a concurrent save from a STALE version conflicts, so the guarded-write resolve must union
    // A's [A1] with B's [B1] and retry, leaving BOTH — co-edit, not clobber.
    qint64 vB = 0;
    {
      auto ready = std::make_shared<bool>(false);
      auto linesB = std::make_shared<QJsonArray>();
      linesB->append(line("B1"));
      guardedSave(B, id, bVersion, linesB, [&](bool committed, qint64 nv) {
        check(committed, "B: stale save resolved via merge + retry (no clobber)");
        vB = nv;
        *ready = true;
      });
      pump(ready);
    }
    check(vB > vA, "B: merged save advanced the version past A's");

    // ── Convergence: the server layout now holds BOTH editors' annotations ──
    {
      auto ready = std::make_shared<bool>(false);
      A->getProjectAsync(id, [&](bool ok, ServerProject meta, QJsonObject layout) {
        check(ok, "final: re-read the converged project");
        check(layoutHas(layout, "A1") && layoutHas(layout, "B1"),
              "co-edit converged: both [A1] and [B1] survive the concurrent save");
        check(meta.version == vB, "final version matches the last committed save");
        *ready = true;
      });
      pump(ready);
    }

    // ── cleanup ──
    {
      auto ready = std::make_shared<bool>(false);
      A->deleteProjectAsync(id, [&](bool) { *ready = true; });
      pump(ready, 5000);
    }
  }

}  // namespace coedit

// The ops that wait on I/O — a connection, a video's frames, a URL, a file or a project to open, a save
// (and the .stc @frame reload): the checks each makes first, then the start on the target, whose answer resumes the run.
#include "planExecutorParts.hpp"

namespace stencil::llm {

  void PlanTarget::connectServerThen(const QString& server, OpDone done) {
    QString err;
    const bool ok = connectServer(server, &err);
    done(ok, err);
  }

  void PlanTarget::extractFramesThen(const QVector<int>& indices, OpDone done) {
    QString err;
    const bool ok = extractFrames(indices, &err);
    done(ok, err);
  }

  void PlanTarget::openUrlThen(const QString& url, bool incognito, OpDone done) {
    QString err;
    const bool ok = openUrl(url, incognito, &err);
    done(ok, err);
  }

  void PlanTarget::openFileThen(const QString& path, OpDone done) {
    QString err;
    const bool ok = openFile(path, &err);
    done(ok, err);
  }

  void PlanTarget::openSourceFrameThen(const QString& spec, int frame, OpDone done) {
    QString err;
    const bool ok = openSourceFrame(spec, frame, &err);
    done(ok, err);
  }

  void PlanTarget::openProjectNamedThen(const QString& name, bool last, OpDone done) {
    QString note;
    const bool ok = openProjectNamed(name, last, &note);
    done(ok, note);
  }

  void PlanTarget::saveProjectThen(const QString& name, const QString& dest, OpDone done) {
    QString err;
    const bool ok = saveProject(name, dest, &err);
    done(ok, err);
  }

  namespace exec {

    bool isAwaitedOp(OpKind op) {
      return op == OpKind::CONNECT || op == OpKind::FRAME || op == OpKind::OPEN_URL ||
             op == OpKind::OPEN_FILE || op == OpKind::OPEN_PROJECT || op == OpKind::SAVE;
    }

  }  // namespace exec

  namespace {

    // Saving nothing is a skipped action, never a failed plan; an unechoed destination (§10) costs
    // the destination, not the save.
    void startSave(const Action& a, PlanTarget& target, QStringList* notes, QString* err,
                   const std::function<void(bool)>& done) {
      if (!target.hasImage()) {
        if (notes) *notes << QStringLiteral("Skipped save — no working image to save");
        return done(true);
      }
      QString dest = a.path;
      if (!dest.isEmpty() && !pathEchoedIn(target.userTypedText(), dest)) {
        if (notes)
          *notes << QStringLiteral("Saved to the usual place — \"%1\" is not a path you "
                                   "gave in this conversation")
                        .arg(dest);
        dest.clear();
      }
      target.saveProjectThen(a.name, dest, [err, done](bool ok, const QString& why) {
        if (!ok) *err = why;
        done(ok);
      });
    }

  }  // namespace

  namespace exec {

    void startAwaitedAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                            QStringList* notes, QString* err, const std::function<void(bool)>& done) {
      const auto refuse = [&](const QString& why) {
        *err = why;
        done(false);
      };
      // A loaded picture is a fresh frame; a connection leaves the frame as it was.
      const auto landing = [map = &frame, err, done](bool freshFrame) {
        return [map, err, done, freshFrame](bool ok, const QString& why) {
          if (!ok) {
            *err = why;
            return done(false);
          }
          if (freshFrame) map->reset();
          done(true);
        };
      };
      switch (a.op) {
        case OpKind::CONNECT:
          // Parse-banned in variants; the guard is defensive only.
          if (inVariant)
            return refuse(QStringLiteral("editor-settings ops are not allowed inside a variant"));
          return target.connectServerThen(a.server, landing(false));
        case OpKind::FRAME:
          if (inVariant) return refuse(QStringLiteral("frame: not valid inside a variant"));
          if (!target.isVideoInput())
            return refuse(QStringLiteral("frame: the current input is not a video"));
          return target.extractFramesThen(a.indices, landing(true));
        case OpKind::OPEN_PROJECT:
          if (inVariant) return refuse(QStringLiteral("editor-settings ops are not allowed inside a variant"));
          // §10's true+note contract: a note (unknown name, declined confirm) is surfaced and the
          // plan goes on over the canvas it left; only an opened project is a fresh frame.
          return target.openProjectNamedThen(
              a.name, a.current, [map = &frame, notes, err, done](bool ok, const QString& note) {
                if (!ok) {
                  *err = note;
                  return done(false);
                }
                if (note.isEmpty()) map->reset();
                else if (notes) *notes << QStringLiteral("openProject: %1").arg(note);
                done(true);
              });
        case OpKind::SAVE:
          if (inVariant) return refuse(QStringLiteral("save: not allowed inside a variant"));
          return startSave(a, target, notes, err, done);
        case OpKind::OPEN_URL:
          if (inVariant) return refuse(QStringLiteral("openUrl: not allowed inside a variant"));
          // The model may only ECHO the user: the exact URL must appear in the user's own messages this
          // conversation (the `connect` stance - a plan can never introduce a host).
          if (!target.userTypedText().contains(a.url))
            return refuse(QStringLiteral(
                              "openUrl blocked: \"%1\" is not a URL you gave in this conversation")
                              .arg(a.url));
          return target.openUrlThen(a.url, a.incognito, landing(true));
        default:
          if (inVariant) return refuse(QStringLiteral("openFile: not allowed inside a variant"));
          // The same echo rule as openUrl, for the filesystem: the assistant reads
          // only where the user themselves pointed it.
          if (!pathEchoedIn(target.userTypedText(), a.path))
            return refuse(QStringLiteral(
                              "openFile blocked: \"%1\" is not a path you gave in this conversation")
                              .arg(a.path));
          return target.openFileThen(a.path, landing(true));
      }
    }

  }  // namespace exec

}  // namespace stencil::llm

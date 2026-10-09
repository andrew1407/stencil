// The linked .stencil file's conflict question (app/remote/StencilFileSync) over stand-in hooks:
// a change or flush that lands while it is open asks nothing twice, "Keep mine" writes the editor
// as it is when answered, and the write replaces the file whole.
#include "StencilFileSync.hpp"
#include "Notifications.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QWidget>

#include "../../support/check.hpp"

using stencil::gui::ConfirmChoice;
using stencil::gui::ConfirmSpec;
using stencil::gui::StencilFileSync;

namespace {
  QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }
  void writeAll(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(bytes);
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir dir;
  check(dir.isValid(), "a scratch dir");
  const QString path = dir.filePath(QStringLiteral("p.stencil"));
  writeAll(path, "base");

  QWidget host;
  auto* notify = new stencil::gui::Notifications(&host);
  QByteArray editor = "base";
  int prompts = 0, applied = 0;
  StencilFileSync* sync = nullptr;
  sync = new StencilFileSync(
      &host, notify,
      StencilFileSync::Hooks{
          [&editor] { return editor; },
          [&applied](const QByteArray&, bool) { ++applied; },
          [](bool) {},
          [&](QWidget*, const ConfirmSpec&) {
            if (++prompts > 1) return ConfirmChoice::CANCEL;
            // While the question is open: the user draws, the file changes again, the debounce fires.
            editor = "mine-later";
            writeAll(path, "theirs-2");
            sync->onFileChanged();
            sync->flushAutosave();
            return ConfirmChoice::CANCEL;
          },
      });
  sync->link(path, "base");
  sync->setLiveSync(true);

  editor = "mine";
  writeAll(path, "theirs");
  sync->onFileChanged();
  check(prompts == 1, "a change or flush during the open question asks no second one");
  check(applied == 0, "keeping mine applies nothing from the file");
  check(readAll(path) == "mine-later", "keep mine writes the editor as it is when answered");
  sync->onFileChanged();
  check(prompts == 1, "our own write is the baseline, so it raises no question");

  // Taking the file's version while a flush waits behind the question asks once, and the flush
  // never writes the editor over the version being taken in.
  {
    const QString taken = dir.filePath(QStringLiteral("t.stencil"));
    writeAll(taken, "base");
    QByteArray mine = "base";
    int asked = 0, applies = 0;
    StencilFileSync* other = nullptr;
    other = new StencilFileSync(
        &host, notify,
        StencilFileSync::Hooks{
            [&mine] { return mine; },
            [&applies](const QByteArray&, bool) { ++applies; },   // lands later, as the decode does
            [](bool) {},
            [&](QWidget*, const ConfirmSpec&) {
              ++asked;
              other->flushAutosave();
              return ConfirmChoice::CONFIRM;
            },
        });
    other->link(taken, "base");
    other->setLiveSync(true);
    mine = "mine";
    writeAll(taken, "theirs");
    other->onFileChanged();
    check(asked == 1 && applies == 1, "taking the file's version asks once and applies it once");
    check(readAll(taken) == "theirs", "the waiting flush leaves the version being taken in alone");
  }

  const QStringList left = QDir(dir.path()).entryList(QDir::Files);
  check(left == (QStringList{QStringLiteral("p.stencil"), QStringLiteral("t.stencil")}),
        "the writes leave no temporary beside the files");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}

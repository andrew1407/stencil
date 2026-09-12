// Headless checks for the DESCRIPTION & ATTRIBUTES editors (dialogs/descriptionDialog,
// dialogs/keywordsDialog) — the browser's description/keywords modals on the shared shell:
//   - structure: glyph + title header with the Close pill, the one text area with its
//     placeholder, the footer hint, and the Cancel / Save CTAs;
//   - the field is pre-filled with the current value; Save returns the trimmed text,
//     Cancel / Escape reject;
//   - keywords: Enter saves, and the words come back lowercase, unique, in order;
//   - apply(): the store write the Projects window's row menu makes, against a temp
//     state dir — the value round-trips through fileStore::loadProjects.
// Offscreen; the modals are driven from a 0-timer inside their own exec() loop.
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "fileStore.hpp"
#include "modalChrome.hpp"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace stencil::gui;

#include "support/check.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static QPushButton* btnByText(QWidget* root, const QString& text) {
  for (QPushButton* b : root->findChildren<QPushButton*>())
    if (b->text() == text) return b;
  return nullptr;
}

static bool headerHasGlyph(QWidget* root) {
  for (QLabel* l : root->findChildren<QLabel*>())
    if (!l->pixmap().isNull()) return true;
  return false;
}

static void pressKey(QWidget* w, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  QKeyEvent down(QEvent::KeyPress, key, mods);
  QApplication::sendEvent(w, &down);
  QKeyEvent up(QEvent::KeyRelease, key, mods);
  QApplication::sendEvent(w, &up);
}

// Run `act` once `dlg` is up inside exec(); it must close the dialog.
static int execWith(QDialog& dlg, std::function<void()> act) {
  QTimer::singleShot(0, [&dlg, act] {
    for (int i = 0; i < 300 && !dlg.isVisible(); ++i) pumpFor(5);
    act();
  });
  return dlg.exec();
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  // An isolated store: apply() writes projects.json under the state dir.
  QTemporaryDir state;
  qputenv("STENCIL_STATE_DIR", state.path().toUtf8());
  QWidget host;
  host.show();

  // ── DescriptionDialog: structure + prefill + Save ──
  {
    DescriptionDialog dlg("An old note", &host);
    QLabel* title = dlg.findChild<QLabel*>("modalTitle");
    check(title && title->text() == "Project description", "description: the shell's title");
    check(headerHasGlyph(&dlg), "description: the header wears a glyph");
    check(dlg.findChild<QPushButton*>("modalClosePill") != nullptr,
          "description: the header carries the Close pill");
    auto* area = dlg.findChild<QPlainTextEdit*>("descriptionText");
    check(area != nullptr, "description: one text area in the body");
    check(area && area->placeholderText() == QString::fromUtf8("Describe this project…"),
          "description: the browser's placeholder");
    check(area && area->toPlainText() == "An old note", "description: pre-filled with the current value");
    QLabel* hint = dlg.findChild<QLabel*>("modalFooterHint");
    check(hint && hint->text() == "Shown in the projects list and its tooltip.",
          "description: the footer hint");
    QPushButton* save = btnByText(&dlg, "Save");
    QPushButton* cancel = btnByText(&dlg, "Cancel");
    check(save && cancel, "description: Cancel + Save CTAs in the footer");
    check(save && !save->icon().isNull() && cancel && !cancel->icon().isNull(),
          "description: both CTAs carry their glyph");
    const int r = execWith(dlg, [&] {
      area->setPlainText("  A fresh description  \n");
      save->click();
    });
    check(r == QDialog::Accepted, "description: Save accepts");
    check(dlg.text() == "A fresh description", "description: the trimmed text comes back");
  }
  {
    DescriptionDialog dlg("keep", &host);
    auto* area = dlg.findChild<QPlainTextEdit*>("descriptionText");
    const int r = execWith(dlg, [&] {
      area->setPlainText("changed");
      pressKey(area, Qt::Key_Return);   // a newline in the area, not a save
      pumpFor(20);
      check(dlg.isVisible(), "description: plain Enter types, it does not save");
      pressKey(&dlg, Qt::Key_Escape);
    });
    check(r == QDialog::Rejected, "description: Escape cancels");
  }
  {
    DescriptionDialog dlg("x", &host);
    auto* area = dlg.findChild<QPlainTextEdit*>("descriptionText");
    const int r = execWith(dlg, [&] {
      area->setPlainText("saved by chord");
      pressKey(area, Qt::Key_Return, Qt::ControlModifier);
    });
    check(r == QDialog::Accepted && dlg.text() == "saved by chord",
          "description: Ctrl+Enter saves from inside the area");
  }
  {
    DescriptionDialog dlg("", &host);
    const int r = execWith(dlg, [&] { btnByText(&dlg, "Cancel")->click(); });
    check(r == QDialog::Rejected, "description: Cancel rejects");
  }

  // ── KeywordsDialog: structure + Enter + normalisation ──
  {
    KeywordsDialog dlg({"alpha", "beta"}, &host);
    QLabel* title = dlg.findChild<QLabel*>("modalTitle");
    check(title && title->text() == "Project keywords", "keywords: the shell's title");
    check(headerHasGlyph(&dlg), "keywords: the header wears a glyph");
    auto* area = dlg.findChild<QPlainTextEdit*>("keywordsText");
    check(area != nullptr, "keywords: one text area in the body");
    check(area && area->placeholderText() == QString::fromUtf8("keyword, another keyword…"),
          "keywords: the browser's placeholder");
    check(area && area->toPlainText() == "alpha, beta", "keywords: pre-filled, comma separated");
    QLabel* hint = dlg.findChild<QLabel*>("modalFooterHint");
    check(hint && hint->text() == QString::fromUtf8("Comma or space separated · used by the projects search."),
          "keywords: the footer hint");
    check(btnByText(&dlg, "Save") && btnByText(&dlg, "Cancel"), "keywords: Cancel + Save CTAs");
    const int r = execWith(dlg, [&] {
      area->setPlainText("Kitchen, plan  remodel,KITCHEN\nfloor");
      pressKey(area, Qt::Key_Return);
    });
    check(r == QDialog::Accepted, "keywords: Enter in the field saves");
    check(dlg.keywords() == QStringList({"kitchen", "plan", "remodel", "floor"}),
          "keywords: lowercase, unique, in order, split on commas/spaces/newlines");
  }
  {
    KeywordsDialog dlg({}, &host);
    const int r = execWith(dlg, [&] { pressKey(&dlg, Qt::Key_Escape); });
    check(r == QDialog::Rejected, "keywords: Escape cancels");
  }
  check(KeywordsDialog::parse("  ") .isEmpty(), "parse: blanks give no keywords");
  check(KeywordsDialog::parse("A,a, b") == QStringList({"a", "b"}), "parse: case-folded duplicates collapse");

  // ── apply(): the store write, round-tripped through the file store ──
  {
    std::vector<Project> projects;
    Project p;
    p.meta.id = "p1";
    p.meta.name = "Kitchen";
    p.meta.updatedAt = 10;
    projects.push_back(p);
    check(!DescriptionDialog::apply(projects, "nope", "x", 20), "apply: an unknown id writes nothing");
    check(DescriptionDialog::apply(projects, "p1", "A sunny kitchen", 20), "apply: description lands");
    check(KeywordsDialog::apply(projects, "p1", {"kitchen", "plan"}, 30), "apply: keywords land");
    check(projects[0].meta.description == "A sunny kitchen" && projects[0].meta.updatedAt == 30,
          "apply: the in-memory list is mutated and stamped");
    const std::vector<Project> back = fileStore::loadProjects();
    check(back.size() == 1 && back[0].meta.description == "A sunny kitchen",
          "apply: the description round-trips through projects.json");
    check(back.size() == 1 && back[0].meta.keywords == std::vector<std::string>({"kitchen", "plan"}),
          "apply: the keywords round-trip through projects.json");
    check(DescriptionDialog::apply(projects, "p1", "", 40) && fileStore::loadProjects()[0].meta.description.empty(),
          "apply: an empty description clears it");
    check(KeywordsDialog::apply(projects, "p1", {}, 50) && fileStore::loadProjects()[0].meta.keywords.empty(),
          "apply: an empty list clears the keywords");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

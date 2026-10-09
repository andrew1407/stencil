// Headless checks for the DESCRIPTION editor and both editors' meta edit (dialogs/descriptionDialog,
// dialogs/keywordsDialog); the keywords FIELD itself is keywordChips.headless.cpp. The browser's
// description modal on the shared shell: its structure, a field pre-filled with the current value where
// Save returns the trimmed text and Cancel/Escape reject, and apply()'s in-memory edit, which leaves the
// temp state dir's projects.json to the caller. Offscreen, driven from a 0-timer inside exec().
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "fileStore.hpp"
#include "modalChrome.hpp"
#include "metaDialogs.hpp"

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

#include "../../support/check.hpp"

// Run `act` once `dlg` is up inside exec(); it must close the dialog.
static int execWith(QDialog& dlg, std::function<void()> act) {
  QTimer::singleShot(0, [&dlg, act] {
    for (int i = 0; i < 300 && !dlg.isVisible(); ++i) pumpFor(5);
    act();
  });
  return dlg.exec();
}

// Hosted the way execMaybePopover hosts a compact popover: a plain child, capped at 420px wide.
static QString clippedAsPopover(QDialog& dlg) {
  QWidget overlay;
  dlg.setParent(&overlay);
  dlg.setWindowFlags(Qt::Widget);
  dlg.setMinimumSize(0, 0);
  dlg.setMaximumSize(420, 560);
  dlg.setGeometry(0, 0, 420, qMin(560, dlg.sizeHint().height()));
  overlay.resize(dlg.size());
  overlay.show();
  pumpFor(30);
  QString clipped;
  for (QWidget* w : dlg.findChildren<QWidget*>())
    if (w->isVisible() && w->width() > 8 && w->mapTo(&dlg, QPoint(w->width(), 0)).x() > dlg.width() + 1)
      clipped += QStringLiteral("%1(%2) ").arg(w->metaObject()->className(), w->objectName());
  overlay.hide();
  dlg.setParent(nullptr);
  return clipped;
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
    // No footer caption: an empty hint means addModalFooter builds no label at all.
    check(!dlg.findChild<QLabel*>("modalFooterHint"),
          "description: no footer caption — the field says what it is");
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

  // ── both fit the popover's 420px cap without clipping a row ──
  {
    DescriptionDialog desc("A note", nullptr);
    const QString d = clippedAsPopover(desc);
    check(d.isEmpty(), qPrintable("description: nothing clipped as a popover: " + d));
    KeywordsDialog kw({"kitchen", "a rather long keyword", "plan"}, nullptr);
    const QString k = clippedAsPopover(kw);
    check(k.isEmpty(), qPrintable("keywords: nothing clipped as a popover: " + k));
  }

  // ── apply(): the in-memory edit; the window's SharedState makes the one store write ──
  {
    std::vector<Project> projects;
    Project p;
    p.meta.id = "p1";
    p.meta.name = "Kitchen";
    p.meta.updatedAt = 10;
    projects.push_back(p);
    check(!DescriptionDialog::apply(projects, "nope", "x", 20), "apply: an unknown id edits nothing");
    check(DescriptionDialog::apply(projects, "p1", "A sunny kitchen", 20), "apply: description lands");
    check(KeywordsDialog::apply(projects, "p1", {"kitchen", "plan"}, 30), "apply: keywords land");
    check(projects[0].meta.description == "A sunny kitchen" && projects[0].meta.updatedAt == 30,
          "apply: the in-memory list is mutated and stamped");
    check(projects[0].meta.keywords == std::vector<std::string>({"kitchen", "plan"}),
          "apply: the keywords replace the old list");
    check(fileStore::loadProjects().empty(), "apply: projects.json is left to the caller's one write");
    check(DescriptionDialog::apply(projects, "p1", "", 40) && projects[0].meta.description.empty(),
          "apply: an empty description clears it");
    check(KeywordsDialog::apply(projects, "p1", {}, 50) && projects[0].meta.keywords.empty(),
          "apply: an empty list clears the keywords");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

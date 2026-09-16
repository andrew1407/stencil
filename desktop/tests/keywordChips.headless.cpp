// Headless checks for the KEYWORDS field (dialogs/KeywordChips — the browser's
// ui/keywordChips.js twin), through the dialog that hosts it:
//   - structure: a line edit + Add over a chip well, each chip an oval with a bare ✕;
//   - Enter ADDS rather than saves; a word already held moves to the front, never doubles;
//   - the ✕ drops one word, Clear all drops them all (and the pending input with them);
//   - a typed phrase is ONE keyword: "kitchen remodel" is never split on its space;
//   - normalize/parse/addTo: the browser twin's own cases, case-folded and de-duplicated.
// Offscreen; the dialog is driven from a 0-timer inside its own exec() loop.
#include "KeywordChips.hpp"
#include "KeywordsDialog.hpp"
#include "modalChrome.hpp"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
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

// The chips, and the words they show, in the order they are LAID OUT — the nodes are
// reused across edits, so child order is creation order and says nothing about the row.
static QList<QFrame*> chips(QWidget* root) {
  QList<QFrame*> out;
  QWidget* area = root->findChild<QWidget*>("keywordsChipArea");
  QLayout* flow = area ? area->layout() : nullptr;
  for (int i = 0; flow && i < flow->count(); ++i) {
    auto* f = qobject_cast<QFrame*>(flow->itemAt(i)->widget());
    // A leaving chip holds its slot while it collapses (playLeave) — it is on screen but
    // no longer one of the list's words.
    if (f && f->property("kwChip").toBool() && !f->property("kwLeaving").toBool()) out << f;
  }
  return out;
}
static QStringList chipWords(QWidget* root) {
  QStringList out;
  for (QFrame* chip : chips(root))
    for (QLabel* l : chip->findChildren<QLabel*>()) out << l->text();
  return out;
}

static void pressKey(QWidget* w, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  QKeyEvent down(QEvent::KeyPress, key, mods);
  QApplication::sendEvent(w, &down);
  QKeyEvent up(QEvent::KeyRelease, key, mods);
  QApplication::sendEvent(w, &up);
}

// Run `act` from inside the dialog's own exec() loop, then return how it was dismissed.
static int execWith(QDialog& dlg, std::function<void()> act) {
  QTimer::singleShot(0, [&] { pumpFor(60); act(); });
  return dlg.exec();
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;

  {   // ── KeywordsDialog: the chips field ──
    KeywordsDialog dlg({"alpha", "beta"}, &host);
    QLabel* title = dlg.findChild<QLabel*>("modalTitle");
    QLabel* hint = dlg.findChild<QLabel*>("modalFooterHint");
    auto* input = dlg.findChild<QLineEdit*>("keywordsInput");
    check(title && title->text() == "Project keywords" && headerHasGlyph(&dlg), "keywords: title + glyph");
    check(!dlg.findChild<QPlainTextEdit*>("keywordsText")
              && input && input->placeholderText() == QString::fromUtf8("Add a keyword…"),
          "keywords: a line edit with the browser's placeholder, and no text area");
    check(dlg.findChild<QWidget*>("keywordsChips") && chipWords(&dlg) == QStringList({"alpha", "beta"}),
          "keywords: a chip well, pre-filled");
    check(!hint, "keywords: no footer caption — the field says it all");
    check(btnByText(&dlg, "Save") && btnByText(&dlg, "Cancel") && btnByText(&dlg, "Add"),
          "keywords: Cancel + Save CTAs, and Add beside the input");
    check(btnByText(&dlg, "Add")->toolTip().isEmpty()
              && btnByText(&dlg, "Clear all")->toolTip().isEmpty(),
          "keywords: Add and Clear all carry no tooltip — their labels say it");

    // Enter ADDS the typed word; it never accepts the dialog, or a half-typed one is lost.
    input->setText("Gamma");
    pressKey(input, Qt::Key_Return);
    check(!dlg.isVisible() && chipWords(&dlg) == QStringList({"gamma", "alpha", "beta"}),
          "keywords: Enter adds the word at the front and does not save");
    check(input->text().isEmpty(), "keywords: the input clears after an add");
    // Already held → moves to the front, never a second chip.
    input->setText("beta");
    btnByText(&dlg, "Add")->click();
    check(chipWords(&dlg) == QStringList({"beta", "gamma", "alpha"}),
          "keywords: a word already held moves to the front instead of doubling");
    check(chips(&dlg).size() == 3, "keywords: one chip per word");
    QPushButton* x = chips(&dlg).first()->findChild<QPushButton*>();
    check(x && x->toolTip().isEmpty(), "keywords: the ✕ carries no tooltip — the word is beside it");
    x->click();
    check(chipWords(&dlg) == QStringList({"gamma", "alpha"}), "keywords: the ✕ drops that word");
    // Clear all empties the list AND the pending input, or Save would put it straight back.
    input->setText("unsent");
    btnByText(&dlg, "Clear all")->click();
    check(chipWords(&dlg).isEmpty() && dlg.keywords().isEmpty(), "keywords: Clear all empties list + input");
    check(!btnByText(&dlg, "Clear all")->isEnabled(), "keywords: Clear all greys out with nothing to clear");
    // A typed phrase is ONE keyword, never split on its spaces.
    input->setText("Kitchen Remodel");
    btnByText(&dlg, "Add")->click();
    check(chipWords(&dlg) == QStringList({"kitchen remodel"}), "keywords: a phrase is one chip, not two");
    check(btnByText(&dlg, "Clear all")->isEnabled(), "keywords: it comes back with the first keyword");

    const int r = execWith(dlg, [&] { btnByText(&dlg, "Save")->click(); });
    check(r == QDialog::Accepted && dlg.keywords() == QStringList({"kitchen remodel"}),
          "keywords: Save accepts, and the chips are the value");
  }
  {
    // Whatever is still typed counts as named — Save never drops it on the floor.
    KeywordsDialog dlg({"held"}, &host);
    dlg.findChild<QLineEdit*>("keywordsInput")->setText("unsent");
    check(dlg.keywords() == QStringList({"unsent", "held"}),
          "keywords: a word still in the input is saved too");
    const int r = execWith(dlg, [&] { pressKey(&dlg, Qt::Key_Escape); });
    check(r == QDialog::Rejected, "keywords: Escape cancels");
  }
  // The browser twin's cases (tests/keywordChips.test.js).
  check(KeywordChips::normalize("kitchen remodel") == "kitchen remodel",
        "normalize: a keyword is whatever was typed, however many words");
  check(KeywordChips::normalize("  Field   Notes  ") == "field notes", "normalize: trimmed, collapsed, lowercased");
  check(KeywordChips::normalize("  ").isEmpty(), "normalize: blanks give no keyword");
  check(KeywordChips::normalize("a, b") == "a, b", "normalize: a comma is part of the keyword, not a separator");
  check(KeywordChips::parse({"Maps", "kitchen remodel", "maps"}) == QStringList({"maps", "kitchen remodel"}),
        "parse: a stored list, cleaned and de-duplicated in order");
  check(KeywordChips::parse({" ", "", QString()}).isEmpty(), "parse: blanks drop out");
  check(KeywordChips::addTo({"b", "c"}, "a") == QStringList({"a", "b", "c"}), "addTo: a new keyword lands first");
  check(KeywordChips::addTo({"a", "b", "c"}, "C") == QStringList({"c", "a", "b"}), "addTo: a held keyword moves first");
  check(KeywordChips::addTo({"kitchen remodel", "x"}, "Kitchen  Remodel") == QStringList({"kitchen remodel", "x"}),
        "addTo: a multi-word keyword already held is not doubled");
  check(KeywordChips::addTo({"a", "b"}, "a") == QStringList({"a", "b"}), "addTo: already first is a no-op");
  check(KeywordChips::addTo({"a", "b"}, " ") == QStringList({"a", "b"}), "addTo: nothing typed changes nothing");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

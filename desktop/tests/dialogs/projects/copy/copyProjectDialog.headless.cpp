// Headless checks for dialogs/projects/copy/CopyProjectDialog (browser ui/modal/copyProjectModal.js):
// the question names the copy, "Make a local copy" shows only for a server source, a server copy
// cannot go incognito, "Just copy" waits while incognito is on, and each box leads its own label
// with what it does as the row's tooltip.
#include "CopyProjectDialog.hpp"
#include "../../../../src/support/motion/ShimmerOverlay.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

#include "../../../support/check.hpp"

using stencil::gui::CopyProjectDialog;

namespace {
  template <typename T> T* named(QWidget& w, const char* name) { return w.findChild<T*>(QString::fromLatin1(name)); }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  {
    CopyProjectDialog dlg(nullptr, "photo", "Image and layout", "photo-copy", QString());
    dlg.show();
    QApplication::processEvents();
    auto* question = named<QLabel>(dlg, "copyProjectQuestion");
    check(question && question->text() == QStringLiteral("Copy “photo” (image and layout) as “photo-copy”?"),
          "the question names the source, the scope and the copy's name, in their own case");
    check(!named<QCheckBox>(dlg, "copyProjectLocal")->isVisible(), "a local source has no local-copy choice");
    auto* incognito = named<QCheckBox>(dlg, "copyProjectIncognito");
    auto* just = named<QPushButton>(dlg, "copyProjectJust");
    check(incognito->isEnabled() && just->isEnabled(), "a local source may copy, open or go incognito");
    incognito->setChecked(true);
    check(!just->isEnabled(), "an incognito copy is only ever opened, so Just copy waits");
    check(incognito->text() == QStringLiteral("Open in incognito") &&
          incognito->toolTip() == QStringLiteral("Open the copy without ever saving it.") &&
          incognito->parentWidget()->toolTip().isEmpty(),
          "the box carries its label and its own tooltip, the row none");
    named<QPushButton>(dlg, "copyProjectNewWindow")->click();
    check(dlg.result() == QDialog::Accepted && dlg.getOutcome() == CopyProjectDialog::OPEN_NEW_WINDOW &&
          dlg.getIncognito(), "Open in new window answers with the incognito pick");
  }

  {
    CopyProjectDialog dlg(nullptr, "roof", "Whole project", "roof-copy", "https://srv.example");
    dlg.show();
    QApplication::processEvents();
    auto* local = named<QCheckBox>(dlg, "copyProjectLocal");
    auto* incognito = named<QCheckBox>(dlg, "copyProjectIncognito");
    check(local->isVisible() && !local->isChecked(), "a server source copies on its server by default");
    check(local->width() < local->parentWidget()->width() / 2 &&
          local->property(stencil::gui::NO_SHIMMER_PROPERTY).toBool() && local->cursor().shape() == Qt::PointingHandCursor,
          "the box hugs its label: the tooltip, the pointer and no hover sweep end there");
    check(local->toolTip() == QStringLiteral("Make it on this computer instead of on https://srv.example."),
          "the local box's tooltip names the server");
    check(local->mapTo(&dlg, QPoint()).x() == incognito->mapTo(&dlg, QPoint()).x(), "the two boxes share one left edge");
    check(!incognito->isEnabled() && incognito->cursor().shape() == Qt::ArrowCursor,
          "a server copy cannot be incognito, and its box stops pointing");
    local->setChecked(true);
    check(incognito->isEnabled(), "a local copy of a server project may go incognito");
    incognito->setChecked(true);
    local->setChecked(false);
    check(!incognito->isEnabled() && !incognito->isChecked(), "back on the server, incognito clears");
    named<QPushButton>(dlg, "copyProjectJust")->click();
    check(dlg.getOutcome() == CopyProjectDialog::JUST_COPY && !dlg.getLocal(), "Just copy on the server");
  }

  {
    CopyProjectDialog dlg(nullptr, "a", "Image only", "a-copy", QString());
    named<QPushButton>(dlg, "copyProjectCancel")->click();
    check(dlg.result() == QDialog::Rejected, "Cancel rejects");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

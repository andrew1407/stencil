#pragma once
// The Open Image suites' common ground: the window they raise it over, the crop toggle, and a
// preview of a source typed into the URL tab.
#include "../../MainWindow.gui.hpp"
#include "OpenImageDialog.hpp"
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>

using stencil::gui::OpenImageDialog;

namespace stencil::guitest {

  // Shown at the size the dialog's crop stage was tuned against.
  inline bool showForOpenImage(MainWindow& win) {
    win.resize(1250, 980);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }

  // The dialog's crop toggle: the one visible check with no text of its own.
  inline QCheckBox* cropBox(gui::OpenImageDialog* dlg) {
    for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
      if (c->isVisible() && c->text().isEmpty()) return c;
    return nullptr;
  }

  struct UrlTab {
    QLineEdit* url = nullptr;
    QPushButton* preview = nullptr;
  };

  // `src` typed into the URL tab and previewed; returns once the picture is up (or 4 s pass).
  inline UrlTab previewByUrl(gui::OpenImageDialog* dlg, const QString& src) {
    auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
    tabs->setCurrentIndex(1);
    QWidget* page = tabs->currentWidget();
    const UrlTab tab{page->findChild<QLineEdit*>(), page->findChild<QPushButton*>()};
    QTest::keyClicks(tab.url, src);
    settle([&] { return tab.preview->isEnabled(); }, 1000);
    tab.preview->click();
    settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
    return tab;
  }

}  // namespace stencil::guitest

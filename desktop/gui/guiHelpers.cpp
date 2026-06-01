#include "guiHelpers.hpp"
#include <QAbstractButton>
#include <QColor>
#include <QDialog>
#include <QIcon>
#include <QPixmap>

namespace stencil::gui {

  QDialogButtonBox* makeButtonBox(QDialog* parent,
                                  QDialogButtonBox::StandardButtons buttons) {
    auto* box = new QDialogButtonBox(buttons, parent);
    QObject::connect(box, &QDialogButtonBox::accepted, parent, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, parent, &QDialog::reject);
    return box;
  }

  void setColorSwatch(QAbstractButton* btn, const QColor& color) {
    if (!btn) return;
    QPixmap chip(20, 20);
    chip.fill(color);
    btn->setIcon(QIcon(chip));
  }

}

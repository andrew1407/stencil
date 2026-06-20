#include "openImageDialog.hpp"
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  OpenImageDialog::OpenImageDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Open Another Image");
    setMinimumWidth(440);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    // File row: a read-only field showing the chosen path + a Browse button that
    // opens the native picker (mirrors the browser modal's file input).
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(this);
    path_->setReadOnly(true);
    path_->setPlaceholderText("No file chosen");
    auto* browse = new QPushButton("Choose File…", this);
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    form->addRow("Image file:", fileRow);

    // Incognito: edit without saving (mirrors the browser checkbox).
    incognito_ = new QCheckBox("Edit without saving", this);
    form->addRow("Incognito:", incognito_);
    layout->addLayout(form);

    auto* hint =
        new QLabel("\"Open here\" replaces the current editor (the current "
                   "project is kept unless it's incognito). \"Open in new "
                   "window\" leaves this editor untouched.",
                   this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    // Three outcomes — Cancel / Open here / Open in new window — instead of a plain
    // OK/Cancel (mirrors the browser's [Cancel] [Open here] [Open in new tab]).
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    here_ = new QPushButton("Open here", this);
    connect(here_, &QPushButton::clicked, this, [this] {
      outcome_ = Outcome::Here;
      accept();
    });
    newWindow_ = new QPushButton("Open in new window", this);
    connect(newWindow_, &QPushButton::clicked, this, [this] {
      outcome_ = Outcome::NewWindow;
      accept();
    });
    btnRow->addWidget(cancel);
    btnRow->addWidget(here_);
    btnRow->addWidget(newWindow_);
    layout->addLayout(btnRow);

    refreshButtons();
  }

  void OpenImageDialog::browse() {
    const QString p = QFileDialog::getOpenFileName(
        this, "Open image", QString(), "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (p.isEmpty()) return;
    path_->setText(p);
    refreshButtons();
  }

  // Both action buttons stay disabled until a file is chosen.
  void OpenImageDialog::refreshButtons() {
    const bool has = !path_->text().isEmpty();
    here_->setEnabled(has);
    newWindow_->setEnabled(has);
  }

  QString OpenImageDialog::path() const { return path_->text(); }
  bool OpenImageDialog::incognito() const { return incognito_->isChecked(); }

}

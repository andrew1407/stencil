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

  OpenImageDialog::OpenImageDialog(QWidget* parent, bool canReplace)
      : QDialog(parent), canReplace_(canReplace) {
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

    // Replace options: only shown when a saved/linked project can be replaced. Rename is
    // off by default; keeping the existing annotations is on (mirrors the browser modal).
    if (canReplace_) {
      rename_ = new QCheckBox("Rename project to the new image", this);
      keep_ = new QCheckBox("Keep existing annotations", this);
      keep_->setChecked(true);
      auto* opts = new QVBoxLayout;
      opts->addWidget(rename_);
      opts->addWidget(keep_);
      form->addRow("Replace:", opts);
    }
    layout->addLayout(form);

    auto* hint =
        new QLabel(canReplace_
                       ? "\"Replace image\" swaps THIS project's image (same project). "
                         "\"Open here\" makes a new project; \"Open in new window\" "
                         "leaves this editor untouched."
                       : "\"Open here\" replaces the current editor (the current "
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
    if (canReplace_) {
      replace_ = new QPushButton("Replace image", this);
      connect(replace_, &QPushButton::clicked, this, [this] {
        outcome_ = Outcome::Replace;
        accept();
      });
      btnRow->addWidget(replace_);
    }
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

  // All action buttons stay disabled until a file is chosen.
  void OpenImageDialog::refreshButtons() {
    const bool has = !path_->text().isEmpty();
    here_->setEnabled(has);
    newWindow_->setEnabled(has);
    if (replace_) replace_->setEnabled(has);
  }

  QString OpenImageDialog::path() const { return path_->text(); }
  bool OpenImageDialog::incognito() const { return incognito_->isChecked(); }
  bool OpenImageDialog::rename() const { return rename_ && rename_->isChecked(); }
  bool OpenImageDialog::keepAnnotations() const { return !keep_ || keep_->isChecked(); }

}

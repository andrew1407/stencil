#include "openImageDialog.hpp"
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Video extensions the loader (MediaLoader) can seek + grab a frame from. A
    // source with one of these — local or in a URL — reveals the frame control.
    bool looksLikeVideo(const QString& src) {
      static const QStringList kExt = {"mp4", "mov", "webm", "mkv", "avi", "m4v", "mpg", "mpeg"};
      const QString s = src.trimmed();
      if (s.isEmpty()) return false;
      // Strip a URL query/fragment before reading the extension.
      QString tail = s.section('/', -1).section('?', 0, 0).section('#', 0, 0);
      return kExt.contains(QFileInfo(tail).suffix().toLower());
    }
  }  // namespace

  OpenImageDialog::OpenImageDialog(QWidget* parent, bool canReplace)
      : QDialog(parent), canReplace_(canReplace) {
    setWindowTitle("Open Another Image");
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    // File row: a read-only field showing the chosen path + a Browse button that
    // opens the native picker (images AND videos, mirroring the browser modal).
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(this);
    path_->setReadOnly(true);
    path_->setPlaceholderText("No file chosen");
    path_->setToolTip("The chosen image or video file (use Choose File… to pick one)");
    auto* browse = new QPushButton("Choose File…", this);
    browse->setToolTip("Browse for an image or video file to open");
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    form->addRow("Image / video:", fileRow);

    // URL row: load an image or video straight from the web (resolved via MediaLoader,
    // which handles CORS-free fetch + video-frame grab). Typing a URL takes precedence
    // over a browsed file. Clears the file field so the two don't conflict.
    url_ = new QLineEdit(this);
    url_->setPlaceholderText("…or paste an image / video URL (https://…)");
    url_->setToolTip("Load an image or video directly from a web URL");
    form->addRow("URL:", url_);

    // Frame row: revealed only for a video source (local or URL). The 0-based frame
    // index to grab as a still (matches the CLI/--frame + MediaLoader contract).
    frame_ = new QSpinBox(this);
    frame_->setRange(0, 1'000'000'000);
    frame_->setToolTip("Which video frame to capture as the still image");
    frameRow_ = new QWidget(this);
    auto* fr = new QHBoxLayout(frameRow_);
    fr->setContentsMargins(0, 0, 0, 0);
    fr->addWidget(frame_);
    fr->addStretch(1);
    form->addRow("Frame:", frameRow_);
    frameRow_->setVisible(false);

    // Incognito: edit without saving (mirrors the browser checkbox).
    incognito_ = new QCheckBox("Edit without saving", this);
    incognito_->setToolTip("Open the image in incognito mode — edits are not saved");
    form->addRow("Incognito:", incognito_);

    // Replace options: only shown when a saved/linked project can be replaced. Rename is
    // off by default; keeping the existing annotations is on (mirrors the browser modal).
    if (canReplace_) {
      rename_ = new QCheckBox("Rename project to the new image", this);
      rename_->setToolTip("Adopt the new file's name for this project");
      keep_ = new QCheckBox("Keep existing annotations", this);
      keep_->setToolTip("Keep the current lines over the replacement image");
      keep_->setChecked(true);
      auto* opts = new QHBoxLayout;
      opts->setSpacing(18);
      opts->addWidget(rename_);
      opts->addWidget(keep_);
      opts->addStretch(1);
      form->addRow("Replace:", opts);
    }
    layout->addLayout(form);

    auto* hint =
        new QLabel(canReplace_
                       ? "\"Replace image\" swaps THIS project's image (same project). "
                         "\"Open here\" makes a new project; \"Open in new window\" "
                         "leaves this editor untouched. A URL or video always makes a "
                         "new project (no in-place replace)."
                       : "\"Open here\" replaces the current editor (the current "
                         "project is kept unless it's incognito). \"Open in new "
                         "window\" leaves this editor untouched.",
                   this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    // Three outcomes — Cancel / Open here / Open in new window (+ Replace when able) —
    // mirroring the browser's [Cancel] [Open here] [Open in new tab].
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setToolTip("Close without opening an image");
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

    // A URL edit re-evaluates the buttons + frame row live; typing a URL blanks the file.
    connect(url_, &QLineEdit::textChanged, this, [this] {
      if (!url_->text().trimmed().isEmpty()) path_->clear();
      refreshButtons();
    });
    refreshButtons();
  }

  void OpenImageDialog::browse() {
    const QString p = QFileDialog::getOpenFileName(
        this, "Open image or video", QString(),
        "Images and video (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.mp4 *.mov *.webm "
        "*.mkv *.avi *.m4v *.mpg *.mpeg);;All files (*)");
    if (p.isEmpty()) return;
    url_->clear();  // a picked file wins over a stale URL
    path_->setText(p);
    refreshButtons();
  }

  // Action buttons stay disabled until a source (file or URL) is chosen; the frame
  // row appears for a video source, and Replace is hidden for URL/video (resolved
  // asynchronously — only a fresh open makes sense). Disabled tooltips explain why.
  void OpenImageDialog::refreshButtons() {
    const bool has = !source().isEmpty();
    const bool video = isVideo();
    frameRow_->setVisible(video);
    here_->setEnabled(has);
    newWindow_->setEnabled(has);
    if (replace_) {
      const bool canReplaceNow = has && !isUrl() && !video;
      replace_->setEnabled(canReplaceNow);
      replace_->setToolTip(
          !has ? "Choose an image file or URL first"
               : (isUrl() || video
                      ? "A URL or video opens as a new project (no in-place replace)"
                      : "Swap this project's image in place (same project)"));
    }
    const QString reason = "Choose an image/video file or paste a URL first";
    here_->setToolTip(has ? "Open the chosen source in this editor (makes a new project)"
                          : reason);
    newWindow_->setToolTip(has ? "Open the chosen source in a new window (this editor stays)"
                               : reason);
  }

  QString OpenImageDialog::source() const {
    const QString u = url_->text().trimmed();
    return u.isEmpty() ? path_->text() : u;
  }
  bool OpenImageDialog::isUrl() const { return !url_->text().trimmed().isEmpty(); }
  bool OpenImageDialog::isVideo() const { return looksLikeVideo(source()); }
  int OpenImageDialog::frame() const { return frame_->value(); }
  bool OpenImageDialog::incognito() const { return incognito_->isChecked(); }
  bool OpenImageDialog::rename() const { return rename_ && rename_->isChecked(); }
  bool OpenImageDialog::keepAnnotations() const { return !keep_ || keep_->isChecked(); }

}

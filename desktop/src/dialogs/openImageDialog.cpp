#include "openImageDialog.hpp"
#include "guiHelpers.hpp"
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Tab order (QTabWidget indices).
    enum { TabFile = 0, TabUrl = 1, TabBlank = 2 };

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

  OpenImageDialog::OpenImageDialog(QWidget* parent, bool canReplace,
                                   int blankW, int blankH, bool startBlank)
      : QDialog(parent), canReplace_(canReplace) {
    setWindowTitle("Open Image");
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    // ── Source tabs: Local file / URL link / Blank. ──
    tabs_ = new QTabWidget(this);

    // Tab: Local file. A read-only field showing the chosen path + a Browse button
    // (images AND videos, mirroring the browser modal).
    auto* fileTab = new QWidget(this);
    auto* fileForm = new QFormLayout(fileTab);
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
    fileForm->addRow("Image / video:", fileRow);
    tabs_->addTab(fileTab, "Local file");

    // Tab: URL link. Load an image or video straight from the web (resolved via
    // MediaLoader, which handles CORS-free fetch + video-frame grab).
    auto* urlTab = new QWidget(this);
    auto* urlForm = new QFormLayout(urlTab);
    url_ = new QLineEdit(this);
    url_->setPlaceholderText("https://… (image or video)");
    url_->setToolTip("Load an image or video directly from a web URL");
    urlForm->addRow("URL:", url_);
    tabs_->addTab(urlTab, "URL link");

    // Tab: Blank. Solid-color canvas (folded in from the retired blank-image dialog).
    auto* blankTab = new QWidget(this);
    auto* blankForm = new QFormLayout(blankTab);
    auto* colorRow = new QHBoxLayout;
    white_ = new QRadioButton("White", this);
    white_->setToolTip("Fill the blank image with white");
    black_ = new QRadioButton("Black", this);
    black_->setToolTip("Fill the blank image with black");
    customColorRadio_ = new QRadioButton("Custom:", this);
    customColorRadio_->setToolTip("Fill the blank image with the picked custom color");
    white_->setChecked(true);
    auto* colorGroup = new QButtonGroup(this);
    colorGroup->addButton(white_);
    colorGroup->addButton(black_);
    colorGroup->addButton(customColorRadio_);
    customSwatch_ = new QToolButton(this);
    customSwatch_->setToolTip("Pick a custom fill color");
    setColorSwatch(customSwatch_, customColor_);
    connect(customSwatch_, &QToolButton::clicked, this, &OpenImageDialog::pickCustomColor);
    colorRow->addWidget(white_);
    colorRow->addWidget(black_);
    colorRow->addWidget(customColorRadio_);
    colorRow->addWidget(customSwatch_);
    colorRow->addStretch(1);
    blankForm->addRow("Fill color:", colorRow);
    blankWidth_ = new QSpinBox(this);
    blankWidth_->setRange(1, 8192);
    blankWidth_->setSuffix(" px");
    blankWidth_->setValue(blankW);
    blankWidth_->setToolTip("Blank image width in pixels (1–8192)");
    blankHeight_ = new QSpinBox(this);
    blankHeight_->setRange(1, 8192);
    blankHeight_->setSuffix(" px");
    blankHeight_->setValue(blankH);
    blankHeight_->setToolTip("Blank image height in pixels (1–8192)");
    blankForm->addRow("Width:", blankWidth_);
    blankForm->addRow("Height:", blankHeight_);
    tabs_->addTab(blankTab, "Blank");
    layout->addWidget(tabs_);

    // Frame row: revealed only for a video source (local or URL). The 0-based frame
    // index to grab as a still (matches the CLI/--frame + MediaLoader contract).
    frame_ = new QSpinBox(this);
    frame_->setRange(0, 1'000'000'000);
    frame_->setToolTip("Which video frame to capture as the still image");
    frameRow_ = new QWidget(this);
    auto* fr = new QHBoxLayout(frameRow_);
    fr->setContentsMargins(0, 0, 0, 0);
    fr->addWidget(new QLabel("Frame:", this));
    fr->addWidget(frame_);
    fr->addStretch(1);
    layout->addWidget(frameRow_);
    frameRow_->setVisible(false);

    // Incognito: edit without saving (mirrors the browser checkbox). Applies to a
    // file/URL open; hidden on the Blank tab (blank creation never honored it).
    commonForm_ = new QFormLayout;
    commonForm_->setContentsMargins(0, 0, 0, 0);
    incognito_ = new QCheckBox("Edit without saving", this);
    incognito_->setToolTip("Open the image in incognito mode — edits are not saved");
    commonForm_->addRow("Incognito:", incognito_);
    layout->addLayout(commonForm_);

    // Replace options: only shown on the Local file tab over a replaceable project.
    replaceRow_ = new QWidget(this);
    if (canReplace_) {
      rename_ = new QCheckBox("Rename project to the new image", this);
      rename_->setToolTip("Adopt the new file's name for this project");
      keep_ = new QCheckBox("Keep existing annotations", this);
      keep_->setToolTip("Keep the current lines over the replacement image");
      keep_->setChecked(true);
      auto* opts = new QHBoxLayout(replaceRow_);
      opts->setContentsMargins(0, 0, 0, 0);
      opts->setSpacing(18);
      opts->addWidget(new QLabel("Replace:", this));
      opts->addWidget(rename_);
      opts->addWidget(keep_);
      opts->addStretch(1);
    }
    layout->addWidget(replaceRow_);

    auto* hint = new QLabel(
        "“Open here” loads the source in this editor; “Open in new window” leaves it "
        "untouched. A URL/video always makes a new project. “Blank” makes a solid-color "
        "canvas.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    // ── Footer actions ── file/URL: Cancel / Replace? / Open here / Open in new window.
    // blank: Cancel / Create blank.
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setToolTip("Close without opening an image");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    here_ = new QPushButton("Open here", this);
    connect(here_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Here; accept(); });
    newWindow_ = new QPushButton("Open in new window", this);
    connect(newWindow_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::NewWindow; accept(); });
    createBlank_ = new QPushButton("Create blank", this);
    connect(createBlank_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Blank; accept(); });
    btnRow->addWidget(cancel);
    if (canReplace_) {
      replace_ = new QPushButton("Replace image", this);
      connect(replace_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Replace; accept(); });
      btnRow->addWidget(replace_);
    }
    btnRow->addWidget(here_);
    btnRow->addWidget(newWindow_);
    btnRow->addWidget(createBlank_);
    layout->addLayout(btnRow);

    // A URL edit re-evaluates the buttons + frame row live; typing a URL blanks the file.
    connect(url_, &QLineEdit::textChanged, this, [this] { refreshButtons(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { applyMode(); });
    tabs_->setCurrentIndex(startBlank ? TabBlank : TabFile);
    applyMode();
  }

  void OpenImageDialog::browse() {
    const QString p = QFileDialog::getOpenFileName(
        this, "Open image or video", QString(),
        "Images and video (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.mp4 *.mov *.webm "
        "*.mkv *.avi *.m4v *.mpg *.mpeg);;All files (*)");
    if (p.isEmpty()) return;
    path_->setText(p);
    refreshButtons();
  }

  void OpenImageDialog::pickCustomColor() {
    const QColor c = QColorDialog::getColor(customColor_, this, "Fill color",
                                            QColorDialog::DontUseNativeDialog);
    if (!c.isValid()) return;
    customColor_ = c;
    customColorRadio_->setChecked(true);
    setColorSwatch(customSwatch_, customColor_);
  }

  // Swap the footer actions to match the active tab. Replace only ever applies to a
  // local-file source over a replaceable project.
  void OpenImageDialog::applyMode() {
    const bool blank = tabs_->currentIndex() == TabBlank;
    commonForm_->setRowVisible(incognito_, !blank);  // incognito has no effect on a blank
    here_->setVisible(!blank);
    newWindow_->setVisible(!blank);
    if (replace_) replace_->setVisible(!blank && tabs_->currentIndex() == TabFile);
    replaceRow_->setVisible(!blank && canReplace_ && tabs_->currentIndex() == TabFile);
    createBlank_->setVisible(blank);
    if (!blank) refreshButtons();
    else frameRow_->setVisible(false);
  }

  // Action buttons stay disabled until a source (file or URL) is chosen; the frame
  // row appears for a video source, and Replace is only for a local-image file.
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
          !has ? "Choose an image file first"
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
    if (tabs_->currentIndex() == TabUrl) return url_->text().trimmed();
    if (tabs_->currentIndex() == TabFile) return path_->text();
    return QString();  // blank tab has no source
  }
  bool OpenImageDialog::isUrl() const { return tabs_->currentIndex() == TabUrl; }
  bool OpenImageDialog::isVideo() const { return looksLikeVideo(source()); }
  int OpenImageDialog::frame() const { return frame_->value(); }
  bool OpenImageDialog::incognito() const { return incognito_->isChecked(); }
  bool OpenImageDialog::rename() const { return rename_ && rename_->isChecked(); }
  bool OpenImageDialog::keepAnnotations() const { return !keep_ || keep_->isChecked(); }

  QColor OpenImageDialog::blankColor() const {
    if (white_->isChecked()) return QColor(Qt::white);
    if (black_->isChecked()) return QColor(Qt::black);
    return customColor_;
  }
  int OpenImageDialog::blankWidth() const { return blankWidth_->value(); }
  int OpenImageDialog::blankHeight() const { return blankHeight_->value(); }

}

// What the dialog answers once it is down: the source, the blank's fill and size, the crop
// choice. Read after exec() returns, so nothing here touches a widget's visibility.
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

namespace stencil::gui {

  void OpenImageDialog::setServerTargets(const QStringList& urls) {
    serverUrls_ = urls;
    target_->clear();
    target_->addItem(tr("Local (this computer)"), QString());
    for (const QString& u : urls) target_->addItem(u, u);
    refreshTargetRow();
  }

  // The row shows only with a server to pick, for a file/URL open that is not incognito.
  void OpenImageDialog::refreshTargetRow() {
    if (!targetRow_) return;
    const bool blank = tabs_->currentIndex() == TabBlank;
    targetRow_->setVisible(!serverUrls_.isEmpty() && !blank && !incognito_->isChecked());
  }

  QString OpenImageDialog::serverTarget() const {
    // isHidden, not isVisible: read after exec() returns, when the dialog is down.
    if (!targetRow_ || targetRow_->isHidden()) return QString();
    return target_->currentData().toString();
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

  // The White/Black presets and the picker all write blank_.color (browser parity:
  // the presets set the same fill the custom swatch holds).
  QColor OpenImageDialog::blankColor() const { return blank_.color; }
  int OpenImageDialog::blankWidth() const { return blank_.width->value(); }
  int OpenImageDialog::blankHeight() const { return blank_.height->value(); }

  bool OpenImageDialog::cropToPage() const { return cropPage_ && cropPage_->isChecked(); }
  bool OpenImageDialog::cropAlbum() const { return cropAlbum_ && cropAlbum_->isChecked(); }
  QString OpenImageDialog::cropPageSize() const { return pageSeed_; }

  // What the user actually dragged. Empty unless a stage is up, so a caller with no rect
  // falls back to the centred crop it always computed.
  core::CropRect OpenImageDialog::cropRect() const {
    return cropStage_ ? cropStage_->cropRect() : core::CropRect{};
  }
}

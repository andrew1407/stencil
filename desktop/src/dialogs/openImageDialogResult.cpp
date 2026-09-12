#include "../support/searchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "openImageDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "../support/underlineTabBar.hpp"
#include "mediaLoader.hpp"
#include <algorithm>
#include <QAudioOutput>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

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

  // The White/Black presets and the picker all write customColor_ (browser parity:
  // the presets set the same fill the custom swatch holds).
  QColor OpenImageDialog::blankColor() const { return customColor_; }
  int OpenImageDialog::blankWidth() const { return blankWidth_->value(); }
  int OpenImageDialog::blankHeight() const { return blankHeight_->value(); }

  bool OpenImageDialog::cropToPage() const { return cropPage_ && cropPage_->isChecked(); }
  bool OpenImageDialog::cropAlbum() const { return cropAlbum_ && cropAlbum_->isChecked(); }
  QString OpenImageDialog::cropPageSize() const {
    return cropPageSize_ ? cropPageSize_->currentData().toString() : QString();
  }
}


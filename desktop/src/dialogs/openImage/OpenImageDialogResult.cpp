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
    serverUrls = urls;
    target->clear();
    target->addItem(tr("Local (this computer)"), QString());
    for (const QString& u : urls) target->addItem(u, u);
    refreshTargetRow();
  }

  // The row shows only with a server to pick, for a file/URL open that is not incognito.
  void OpenImageDialog::refreshTargetRow() {
    if (!targetRow) return;
    const bool blank = tabs->currentIndex() == TabBlank;
    targetRow->setVisible(!serverUrls.isEmpty() && !blank && !incognito->isChecked());
  }

  QString OpenImageDialog::serverTarget() const {
    // isHidden, not isVisible: read after exec() returns, when the dialog is down.
    if (!targetRow || targetRow->isHidden()) return QString();
    return target->currentData().toString();
  }

  QString OpenImageDialog::source() const {
    if (tabs->currentIndex() == TabUrl) return url->text().trimmed();
    if (tabs->currentIndex() == TabFile) return path->text();
    return QString();  // blank tab has no source
  }
  bool OpenImageDialog::isUrl() const { return tabs->currentIndex() == TabUrl; }
  bool OpenImageDialog::isVideo() const { return looksLikeVideo(source()); }
  int OpenImageDialog::getFrame() const { return frame->value(); }
  bool OpenImageDialog::getIncognito() const { return incognito->isChecked(); }
  bool OpenImageDialog::getRename() const { return rename && rename->isChecked(); }
  bool OpenImageDialog::keepAnnotations() const { return !keep || keep->isChecked(); }

  // The White/Black presets and the picker all write blank.color (browser parity:
  // the presets set the same fill the custom swatch holds).
  QColor OpenImageDialog::blankColor() const { return blank.color; }
  int OpenImageDialog::blankWidth() const { return blank.width->value(); }
  int OpenImageDialog::blankHeight() const { return blank.height->value(); }

  bool OpenImageDialog::cropToPage() const { return cropPage && cropPage->isChecked(); }
  bool OpenImageDialog::getCropAlbum() const { return cropAlbum && cropAlbum->isChecked(); }
  QString OpenImageDialog::getCropPageSize() const { return pageSeed; }

  // What the user actually dragged. Empty unless a stage is up, so a caller with no rect
  // falls back to the centred crop it always computed.
  core::CropRect OpenImageDialog::cropRect() const {
    return cropStage ? cropStage->cropRect() : core::CropRect{};
  }
}

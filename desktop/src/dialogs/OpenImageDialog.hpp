#pragma once
#include "CropDialog.hpp"   // CropPreview: the SAME crop stage the editor uses
#include "openImageDialogState.hpp"
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QUrl>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QTabWidget;
class QVBoxLayout;
class QWidget;
class QTimer;
class QVideoFrame;

// The GUI e2e drives the VIDEO branch through this seam (no decoder offscreen).
class MainWindowGuiTest;

// Unified "Open Image" dialog (browser/js/ui/openImageModal.js): the single way into the
// editor — a local FILE, a web URL/reference, or a NEW BLANK canvas — with a live preview
// (video frames seek-able) and an optional page-aspect crop, opening here or in a new
// window. exec(); on Accepted read outcome() and the matching getters.
// Preview, scrub and quick-crop mirror LinksDialog's add-by-URL section, accessor names
// included, so MainWindow consumes the two dialogs identically.
namespace stencil::core { struct PageSize; }

namespace stencil::gui {

  class MediaLoader;

  class OpenImageDialog : public QDialog {
    Q_OBJECT
    friend class ::MainWindowGuiTest;

   public:
    enum class Outcome { HERE, NEW_WINDOW, REPLACE, BLANK };

    // `canReplace` adds the "Replace image" outcome + its rename/keep options (a saved
    // project only); blankW/blankH seed the new-blank size; startBlank opens in blank mode;
    // `pageSeed` ("A3") preselects the crop page.
    explicit OpenImageDialog(QWidget* parent, bool canReplace,
                             int blankW, int blankH, bool startBlank = false,
                             const QString& pageSeed = QStringLiteral("A3"));

    // The chosen source: the URL when one is typed, else the browsed local path.
    QString source() const;
    bool isUrl() const;     // a URL was typed (vs a local file)
    bool isVideo() const;   // the source is a video (local or URL) → grab a frame
    int frame() const;      // 0-based video frame to grab (ignored for still images)
    bool incognito() const;
    bool rename() const;
    bool keepAnnotations() const;
    // The browser's "Save to" row (base.js fillTargetSelect): the connected servers a
    // file/URL open can create on — shown only with servers, never incognito. Empty = local.
    void setServerTargets(const QStringList& urls);
    QString serverTarget() const;

    QColor blankColor() const;
    int blankWidth() const;
    int blankHeight() const;

    // The image/frame decoded for the preview — null until one succeeds. The caller adopts
    // these exact pixels (no second download/seek), as LinksDialog::previewedImage() does.
    QImage previewedImage() const { return previewImage_; }
    // Quick crop (mirrors LinksDialog): cropToPage() off loads the full frame; on, it crops
    // to cropPageSize() in cropAlbum() or portrait.
    bool cropToPage() const;
    bool cropAlbum() const;
    QString cropPageSize() const;
    // The dragged rect in ORIGINAL-image pixels; empty ⇒ the caller centres the crop.
    core::CropRect cropRect() const;

    Outcome outcome() const { return outcome_; }

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;   // Enter previews the URL
    void showEvent(QShowEvent* event) override;    // measures every tab, opens at the tallest
    void hideEvent(QHideEvent* event) override;     // a cloud outlives its tab / popover
    void resizeEvent(QResizeEvent* event) override;  // a wider window fits a bigger preview

   private:
    // What a tab last decoded, re-shown on a switch back instead of re-fetched. Indexed
    // by TabFile / TabUrl; Blank never caches.
    struct TabPreviewCache {
      bool valid = false;
      QString source;
      bool isVideo = false;
      QImage previewImage, frameImage;
      QString hint;
      double scrubFps = 30.0;
      qint64 scrubDurationMs = 0;
      QUrl resolvedUrl;
      core::CropRect cropRect;   // the dragged rect, in THIS decode's own pixels
      bool cropRectValid = false;
    };

    // The ctor's three build steps, one TU each; all three fill the scroll body's column.
    void buildTabs(QVBoxLayout* layout, int blankW, int blankH);
    void buildPreviewColumn(QVBoxLayout* layout);
    void buildCropRows(QVBoxLayout* layout);

    void browse();
    void pickCustomColor();
    void applyMode();       // swap the footer actions to match the active tab
    void fadeInCurrentPage();  // ease the arriving tab page in (browser-parity soft swap)
    void refreshButtons();  // enable file/URL actions once a source is chosen

    void doPreview();             // fetch/decode the current source into the preview
    void resetPreviewState();     // clear preview + disable the open buttons (source changed)
    void updateVideoPreview();    // show the seeked frame as the video's preview
    void fitTabsToCurrentPage();        // the pane hugs the page on show
    void clampToScreen();               // keep a taller/shorter dialog within the monitor
    void refitWindowHeight();           // re-measure and resize the window to its content
    void growPopoverToContent();        // …or, as a popover, the overlay that frames it
    int wantedHeight() const;           // what the scroll body's content asks for
    QSize previewFitBox() const;   // the preview/crop stage's box (OpenImageDialogFit.cpp)
    void applyPreviewFit();        // re-fit the picture and the stage into it
    int shrinkPreviewToFit(int over);   // …smaller by `over` px, so the body needn't scroll
    HeightEase size_;     // the window-height ease and what it measures
    void animateHeightTo(int h);        // ease the window there instead of jumping
    void setHeightNow(int h);           // …and the popover's frame moves with it
    RowMotion motion_;    // one clock per row that forms and falls
    void dropDerived();                 // the fetch, pixels, video read and frame row
    void stalePreview();                // …dropped, the picture kept (the url moved on)
    void setHint(const QString& text);   // muted status line, hidden when empty
    void clearPreviewImage();           // drop the preview pixmap and its box
    void showPreview(const QImage& img, const QString& hint);  // render the preview
    void cacheTabPreview(const QString& src, const QString& hint);  // stash it for a tab switch back
    bool restoreTabPreview(const TabPreviewCache& cache);  // …and bring it back, no re-fetch
    void gatherPreviewDust(const QPixmap& shot);   // the preview's own arrival flourish
    void cropDimsDust(bool arriving);   // the read-out forms / falls with the crop
    void slideCropDims(bool show);      // …its line easing into that place, and out
    void cropSizeRowDust(bool arriving);   // the page-size row forms / falls with the crop
    void slideCropSizeRow(bool show);      // …its own line easing into place, and out
    void cropAlbumDust(bool arriving);     // the Album/Portrait button's own materialize
    void cropSizeCustomDust(bool arriving);   // the Custom W×H group's own materialize
    void slideCropSizeCustom(bool show);      // …its own line easing into place, and out
    void scatterPreviewDust();          // the picture blows away, making room
    void cancelPreviewDust();           // …and every cloud stops, veils lifted
    void setFrame(int n);         // sync slider + spin box to a frame, then debounce a seek
    void applyFrameBounds();      // bound slider/spin box to the video's frame count
    void setupScrubPlayer(const QUrl& url);  // persistent player loaded once for scrubbing
    void teardownScrubPlayer();
    void seekScrub(int frame);    // seek the persistent player to a frame and render it
    void onScrubFrame(const QVideoFrame& frame);  // adopt a rendered frame into the preview
    void showQuickcrop(int w, int h);  // reveal + default the quick-crop row for a preview
    void syncCropStage();              // build / drop the crop stage as Crop is toggled
    void refreshCropDims();            // the stage's size line under it
    core::PageSize cropPageDims() const;   // cropPageSize_/cropSizeW_/H_ resolved — the
                                            // CROP's own ratio, never pageSeed_ (the project's)
    void syncCropPageChoice();         // a different ratio picked: refit the stage to it
    void persistCropRect();            // …and its tab's own copy of the drag
    void syncQuickcropEnabled();       // album/page shown only while cropping to page
    void refreshOpenEnabled();         // gate the open buttons on a resolved preview
    void refreshTargetRow();           // the Save-to row follows servers / incognito / tab

    // Source tabs: 0 = Local file, 1 = URL link, 2 = Blank.
    QTabWidget* tabs_ = nullptr;

    QLineEdit* path_ = nullptr;
    QLineEdit* url_ = nullptr;
    QSpinBox* frame_ = nullptr;
    QSlider* frameSlider_ = nullptr;  // scrub the frame; synced with frame_ (video only)
    QWidget* frameRow_ = nullptr;     // video frame controls — shown only for video
    QCheckBox* incognito_ = nullptr;
    QWidget* incogRow_ = nullptr;  // the Incognito .vs-row (hidden on the Blank tab)
    QCheckBox* rename_ = nullptr;
    QCheckBox* keep_ = nullptr;
    QComboBox* target_ = nullptr;     // "Save to": local or a connected server
    QWidget* targetRow_ = nullptr;
    QStringList serverUrls_;

    QPushButton* previewBtn_ = nullptr;
    QLabel* previewLabel_ = nullptr;  // the rendered image/frame
    QLabel* previewHint_ = nullptr;   // status / dimensions / errors

    QWidget* quickcropRow_ = nullptr;
    QCheckBox* cropPage_ = nullptr;
    QLabel* cropDims_ = nullptr;         // the read-out, centred UNDER the stage
    CropPreview* cropStage_ = nullptr;   // the draggable crop box over the preview
    QWidget* cropStageHost_ = nullptr;   // its slot in the preview column
    QPushButton* cropAlbum_ = nullptr;   // the Album / Portrait toggle (checked = album)
    // The CROP's own page choice — starts from pageSeed_, but picking a different one (Custom
    // included) here affects only this preview's aspect, never the project's own page.
    QWidget* cropSizeRow_ = nullptr;
    QComboBox* cropPageSize_ = nullptr;
    QWidget* cropSizeCustomGroup_ = nullptr;   // W × H, shown only when "custom" is picked
    QDoubleSpinBox* cropSizeW_ = nullptr;
    QDoubleSpinBox* cropSizeH_ = nullptr;
    QString pageSeed_ = "A3";  // canonical format name (findData miss ⇒ A3)

    BlankControls blank_;  // the blank tab's fill and size

    OpenActions act_;     // the footer outcomes + the replace options

    MediaLoader* preview_ = nullptr;    // detects image vs video, grabs the first frame
    QTimer* fetchTimer_ = nullptr;      // debounce seeks while scrubbing
    ScrubPlayer scrub_;   // the persistent player a video is scrubbed with
    QImage previewImage_;       // pixels the open will adopt (frame or preview)
    QString previewedSource_;   // the source the shown preview was fetched for
    QImage frameImage_;                 // last grabbed video frame
    bool previewIsVideo_ = false;       // last preview resolved as a video

    TabPreviewCache tabCache_[2];
    bool tabCrop_[2] = {false, false};   // each tab's own Crop choice (browser tabCrop)

    bool canReplace_ = false;
    bool constructed_ = false;  // gates the tab-switch fade until the dialog is built
    bool measured_ = false;     // first-show tallest-tab measurement ran (showEvent)
    bool measuring_ = false;    // …and is running right now (no fade on its switches)
    Outcome outcome_ = Outcome::HERE;
  };

}

#pragma once
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QString>
#include <QUrl>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QToolButton;
class QSpinBox;
class QSlider;
class QTabWidget;
class QFormLayout;
class QWidget;
class QTimer;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QVideoFrame;

// Unified "Open Image" dialog. Mirrors browser/js/ui/openImageModal.js: the single way
// to get an image into the editor — a local FILE, a web URL/reference, or a NEW BLANK
// canvas — chosen with the Source selector. For a file/URL source, a live PREVIEW shows
// the decoded image (or the chosen video frame, seek-able) before committing, and an
// optional page-aspect CROP (off by default) can be applied on open. Choose to replace
// the current editor ("Open here") or launch it in a new window. For the blank source,
// pick a fill color + size and "Create blank". exec(); on QDialog::Accepted read
// outcome() and the matching getters (rejected = canceled).
//
// The preview + video-scrub + quick-crop machinery deliberately mirrors LinksDialog's
// add-by-URL section (same QVideoSink scrubbing, same crop model + accessor names —
// previewedImage()/cropToPage()/cropAlbum()/cropPageSize()), so MainWindow consumes the
// two dialogs identically.
namespace stencil::gui {

  class MediaLoader;

  class OpenImageDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Outcome { Here, NewWindow, Replace, Blank };

    // `canReplace` enables the "Replace image" outcome + its rename/keep-annotations
    // options (only meaningful when a saved/linked project is open). blankW/blankH seed
    // the new-blank size (the current page at 96 dpi). startBlank opens straight in
    // blank mode (the idle-canvas / projects "new blank" shortcuts). `pageSeed` (a
    // canonical format name, e.g. "A3") preselects the quick-crop page size; `units`
    // ("cm"/"in") is the display unit its page-size labels render in.
    explicit OpenImageDialog(QWidget* parent, bool canReplace,
                             int blankW, int blankH, bool startBlank = false,
                             const QString& pageSeed = QStringLiteral("A3"),
                             const QString& units = QStringLiteral("cm"));

    // ── File / URL source ──
    // The chosen source: the URL when one is typed, else the browsed local path.
    QString source() const;
    bool isUrl() const;     // a URL was typed (vs a local file)
    bool isVideo() const;   // the source is a video (local or URL) → grab a frame
    int frame() const;      // 0-based video frame to grab (ignored for still images)
    bool incognito() const;
    bool rename() const;
    bool keepAnnotations() const;

    // ── New-blank source ──
    QColor blankColor() const;
    int blankWidth() const;
    int blankHeight() const;

    // ── Preview + quick-crop (file / URL source) ──
    // The image/frame decoded for the preview — null until a preview succeeds. When
    // set, the caller adopts these exact pixels (no second download/seek), so what was
    // previewed is exactly what loads (mirrors LinksDialog::previewedImage()).
    QImage previewedImage() const { return previewImage_; }
    // Quick pre-load crop (mirrors LinksDialog): open cropped to a page aspect /
    // orientation, or uncropped. cropToPage() OFF (default) ⇒ load the full frame; on
    // ⇒ crop centered to cropPageSize() in cropAlbum() (landscape) or portrait.
    bool cropToPage() const;
    bool cropAlbum() const;
    QString cropPageSize() const;

    Outcome outcome() const { return outcome_; }

   protected:
    // Enter in the URL field previews instead of accepting the dialog.
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    void browse();
    void pickCustomColor();
    void applyMode();       // swap the footer actions to match the active tab
    void refreshButtons();  // enable file/URL actions once a source is chosen

    // Preview + video scrub (ported from LinksDialog for behavioral parity).
    void doPreview();             // fetch/decode the current source into the preview
    void resetPreviewState();     // clear preview + disable the open buttons (source changed)
    void updateVideoPreview();    // choose embedded preview vs seeked frame for a video
    void showPreview(const QImage& img, const QString& hint);  // render the preview
    void setFrame(int n);         // sync slider + spin box to a frame, then debounce a seek
    void applyFrameBounds();      // bound slider/spin box to the video's frame count
    void setupScrubPlayer(const QUrl& url);  // persistent player loaded once for scrubbing
    void teardownScrubPlayer();
    void seekScrub(int frame);    // seek the persistent player to a frame and render it
    void onScrubFrame(const QVideoFrame& frame);  // adopt a rendered frame into the preview
    void showQuickcrop(int w, int h);  // reveal + default the quick-crop row for a preview
    void syncQuickcropEnabled();       // album/page enabled only while cropping to page
    void refreshOpenEnabled();         // gate the open buttons on a resolved preview

    // Source tabs: 0 = Local file, 1 = URL link, 2 = Blank.
    QTabWidget* tabs_ = nullptr;

    // File / URL controls.
    QLineEdit* path_ = nullptr;
    QLineEdit* url_ = nullptr;
    QSpinBox* frame_ = nullptr;
    QSlider* frameSlider_ = nullptr;  // scrub the frame; synced with frame_ (video only)
    QLabel* frameTotal_ = nullptr;    // "/ N" total-frames hint
    QCheckBox* usePreview_ = nullptr; // "use the video's preview image" (video only)
    QWidget* frameRow_ = nullptr;     // video frame controls — shown only for video
    QCheckBox* incognito_ = nullptr;
    QFormLayout* commonForm_ = nullptr;  // holds the Incognito row (hidden on the Blank tab)
    QCheckBox* rename_ = nullptr;
    QCheckBox* keep_ = nullptr;
    QWidget* replaceRow_ = nullptr;

    // Preview area.
    QPushButton* previewBtn_ = nullptr;
    QLabel* previewLabel_ = nullptr;  // the rendered image/frame
    QLabel* previewHint_ = nullptr;   // status / dimensions / errors

    // Quick-crop row (shown once a preview resolves): crop-to-page toggle (off by
    // default), album/portrait toggle, and the page-size choice.
    QWidget* quickcropRow_ = nullptr;
    QCheckBox* cropPage_ = nullptr;
    QCheckBox* cropAlbum_ = nullptr;
    QComboBox* cropPageSize_ = nullptr;
    QString pageSeed_ = "A3";  // canonical format name (findData miss ⇒ A3)
    QString units_ = "cm";

    // Blank controls.
    QRadioButton* white_ = nullptr;
    QRadioButton* black_ = nullptr;
    QRadioButton* customColorRadio_ = nullptr;
    QToolButton* customSwatch_ = nullptr;
    QSpinBox* blankWidth_ = nullptr;
    QSpinBox* blankHeight_ = nullptr;
    QColor customColor_{Qt::white};

    // Footer actions.
    QPushButton* here_ = nullptr;
    QPushButton* newWindow_ = nullptr;
    QPushButton* replace_ = nullptr;
    QPushButton* createBlank_ = nullptr;

    // Preview state (ported from LinksDialog).
    MediaLoader* preview_ = nullptr;    // detects image vs video, grabs the first frame
    QTimer* fetchTimer_ = nullptr;      // debounce seeks while scrubbing
    QMediaPlayer* scrubPlayer_ = nullptr;
    QAudioOutput* scrubAudio_ = nullptr;
    QVideoSink* scrubSink_ = nullptr;
    double scrubFps_ = 30.0;
    qint64 scrubDurationMs_ = 0;
    qint64 scrubTargetMs_ = 0;
    bool scrubPending_ = false;         // awaiting a rendered frame at the seek target
    QImage previewImage_;               // pixels the open will adopt (frame or preview)
    QImage frameImage_;                 // last grabbed video frame
    QImage thumbImage_;                 // video's embedded preview image, if any
    bool previewIsVideo_ = false;       // last preview resolved as a video

    bool canReplace_ = false;
    Outcome outcome_ = Outcome::Here;
  };

}

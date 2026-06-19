#pragma once
#include <QDialog>
#include <QImage>
#include <QString>
#include <QUrl>

class QLineEdit;
class QSpinBox;
class QLabel;
class QWidget;
class QPushButton;
class QCheckBox;
class QSlider;
class QTimer;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QVideoFrame;

// Source/resource links dialog. Mirrors browser/js/ui/linksModal.js: view, edit,
// open, and remove the active image's provenance — the image/video's own URL
// (source) and the web page it came from (resource) — and add a new image BY URL
// (extracting a video frame when needed). "Open in browser" launches the OS
// default browser (QDesktopServices); the add-by-URL load previews the URL in the
// dialog and, on "Load into editor", hands the already-decoded pixels back to the
// caller (so what was previewed is exactly what loads).
namespace stencil::gui {

  class MediaLoader;

  class LinksDialog : public QDialog {
    Q_OBJECT
   public:
    // `source`/`resource` seed the current-links fields. `hasImage` selects the
    // mode: with an image loaded the dialog only edits its links; with no image it
    // only offers the add-by-URL loader (the URL becomes the source).
    explicit LinksDialog(const QString& source, const QString& resource,
                         bool hasImage, QWidget* parent = nullptr);

    // Edited current-image links (read on a plain OK).
    QString source() const;
    QString resource() const;

    // Add-by-URL request: true when the user clicked "Load into editor" instead of
    // OK. The caller adopts previewedImage() (the pixels already decoded for the
    // preview), tagging it with urlSource()/urlResource() as provenance.
    bool loadRequested() const { return loadRequested_; }
    QString urlSource() const;
    QString urlResource() const;
    int urlFrame() const;

    // The image/frame decoded for the preview — null until a preview succeeds.
    // Load is only enabled once this is set, so on loadRequested() it is non-null.
    QImage previewedImage() const { return previewImage_; }

   protected:
    // Pressing Enter in the URL fields triggers Preview (not the dialog's default
    // button, which would close it). Consumed via an event filter on those fields.
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    void openInBrowser(const QLineEdit* field) const;
    void requestLoad();
    void doPreview();             // fetch/decode the URL into the preview area
    void resetPreviewState();     // clear preview + disable Load (URL changed)
    void updateVideoPreview();    // choose embedded preview vs frame for a video
    void showPreview(const QImage& img, const QString& hint);  // render + enable Load
    void setFrame(int n);         // sync slider + spin box to a frame, then debounce a seek
    void applyFrameBounds();      // bound slider/spin box to the video's frame count
    void setupScrubPlayer(const QUrl& url);  // persistent player loaded once for scrubbing
    void teardownScrubPlayer();
    void seekScrub(int frame);    // seek the persistent player to a frame and render it
    void onScrubFrame(const QVideoFrame& frame);  // adopt a rendered frame into the preview

    QLineEdit* sourceEdit_ = nullptr;
    QLineEdit* resourceEdit_ = nullptr;
    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* urlResourceEdit_ = nullptr;
    QSpinBox* frame_ = nullptr;
    QSlider* frameSlider_ = nullptr;    // scrub the frame; synced with frame_ (video only)
    QLabel* frameTotal_ = nullptr;      // "/ N" total-frames hint
    QCheckBox* usePreview_ = nullptr;   // "use the video's preview image" (video only)
    QWidget* frameRow_ = nullptr;       // "Video frame" controls — shown only for video
    QLabel* previewLabel_ = nullptr;    // the rendered image/frame
    QLabel* previewHint_ = nullptr;     // status / dimensions / errors
    QPushButton* loadBtn_ = nullptr;
    MediaLoader* preview_ = nullptr;    // detects image vs video, grabs the first frame
    QTimer* fetchTimer_ = nullptr;      // debounce seeks while scrubbing

    // Persistent player for live scrubbing: loaded once, then seeked per frame so the
    // preview updates immediately (re-streaming per frame never seeks reliably).
    QMediaPlayer* scrubPlayer_ = nullptr;
    QAudioOutput* scrubAudio_ = nullptr;
    QVideoSink* scrubSink_ = nullptr;
    double scrubFps_ = 30.0;
    qint64 scrubDurationMs_ = 0;
    qint64 scrubTargetMs_ = 0;
    bool scrubPending_ = false;         // awaiting a rendered frame at the seek target

    QImage previewImage_;               // pixels that Load will adopt (frame or preview)
    QImage frameImage_;                 // last grabbed video frame
    QImage thumbImage_;                 // video's embedded preview image, if any
    bool previewIsVideo_ = false;       // last preview resolved as a video
    bool loadRequested_ = false;
  };

}

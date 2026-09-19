#pragma once
#include <QDialog>
#include <QImage>
#include <QString>
#include <QUrl>

class QLineEdit;
class QSpinBox;
class QLabel;
class QWidget;
class QFormLayout;
class QPushButton;
class QCheckBox;
class QComboBox;
class QSlider;
class QTimer;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QVideoFrame;

// Source/resource links dialog (browser/js/ui/linksModal.js). The add-by-URL load hands the
// already-decoded preview pixels back, so what was previewed is exactly what loads.
namespace stencil::gui {

  class MediaLoader;

  class LinksDialog : public QDialog {
    Q_OBJECT
   public:
    // `hasImage` selects the mode: edit the links, or (no image) the add-by-URL loader only. `pageSeed`
    // preselects the quick-crop page size (unknown names fall back to A3); `units` is "cm"/"in".
    explicit LinksDialog(const QString& source, const QString& resource,
                         bool hasImage, const QString& pageSeed = "A3",
                         const QString& units = "cm",
                         QWidget* parent = nullptr);

    // Browser parity: edits apply on ANY close — there is no Cancel/Save pair.
    QString source() const;
    QString resource() const;

    // True when the user clicked "Load into editor"; the caller adopts previewedImage().
    bool loadRequested() const { return loadRequested_; }
    QString urlSource() const;
    QString urlResource() const;
    int urlFrame() const;

    // Browser linksModal parity; read on loadRequested(). cropToPage() off ⇒ the full frame.
    bool cropToPage() const;
    bool cropAlbum() const;
    QString cropPageSize() const;

    // Null until a preview succeeds; Load is only enabled once set.
    QImage previewedImage() const { return previewImage_; }

   protected:
    // Enter in the URL fields triggers Preview, not the dialog's default button (which would close it).
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    // Constructor build stages, each its own TU (LinksDialogQuickCrop / LinksDialogPreviewWiring).
    void buildQuickCrop(QFormLayout* addForm, const QString& units);
    void wirePreview(QPushButton* previewBtn);

    void openInBrowser(const QLineEdit* field) const;
    void requestLoad();
    void doPreview();
    void resetPreviewState();
    void updateVideoPreview();
    void showPreview(const QImage& img, const QString& hint);
    void setFrame(int n);
    void applyFrameBounds();
    void setupScrubPlayer(const QUrl& url);
    void teardownScrubPlayer();
    void seekScrub(int frame);
    void onScrubFrame(const QVideoFrame& frame);
    void showQuickcrop(int w, int h);
    void syncQuickcropEnabled();

    QLineEdit* sourceEdit_ = nullptr;
    QLineEdit* resourceEdit_ = nullptr;
    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* urlResourceEdit_ = nullptr;
    QSpinBox* frame_ = nullptr;
    QSlider* frameSlider_ = nullptr;
    QLabel* frameTotal_ = nullptr;
    QCheckBox* usePreview_ = nullptr;
    QWidget* frameRow_ = nullptr;
    QLabel* previewLabel_ = nullptr;
    QLabel* previewHint_ = nullptr;
    QWidget* quickcropRow_ = nullptr;
    QCheckBox* cropPage_ = nullptr;
    QCheckBox* cropAlbum_ = nullptr;
    QComboBox* cropPageSize_ = nullptr;
    QString pageSeed_ = "A3";
    QPushButton* loadBtn_ = nullptr;
    MediaLoader* preview_ = nullptr;
    QTimer* fetchTimer_ = nullptr;

    // Loaded once, then seeked per frame — re-streaming per frame never seeks reliably.
    QMediaPlayer* scrubPlayer_ = nullptr;
    QAudioOutput* scrubAudio_ = nullptr;
    QVideoSink* scrubSink_ = nullptr;
    double scrubFps_ = 30.0;
    qint64 scrubDurationMs_ = 0;
    qint64 scrubTargetMs_ = 0;
    bool scrubPending_ = false;

    QImage previewImage_;
    QImage frameImage_;
    QImage thumbImage_;
    bool previewIsVideo_ = false;
    bool loadRequested_ = false;
  };

}

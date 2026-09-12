#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QVideoFrame;
class QTimer;

// Resolves a launch --src (local image, remote URL via fetchGuard, or a video frame) into one QImage.
namespace stencil::gui {

  // Pure suffix sniffers off the canon (browser/js/config/mediaTypes.json `surfaces.desktop`).
  bool isVideoFileName(const QString& path);
  bool isImageFileName(const QString& path);

  class MediaLoader : public QObject {
    Q_OBJECT
   public:
    explicit MediaLoader(QObject* parent = nullptr);
    ~MediaLoader() override;

    // `frame` is the 0-based video frame. Emits loaded() or failed() exactly once; a new load() cancels.
    void load(const QString& src, int frame);

    // Valid when loaded() fires.
    bool isVideoSource() const { return isVideo_; }

    // Embedded cover/thumbnail (QMediaMetaData), null when absent. Valid when loaded() fires.
    QImage embeddedThumbnail() const { return thumbnail_; }

    // Valid when loaded() fires for a video; frameCount() 0 = unknown.
    double frameRate() const { return fps_; }
    qint64 durationMs() const { return durationMs_; }
    int frameCount() const {
      return (fps_ > 0 && durationMs_ > 0) ? static_cast<int>(durationMs_ / 1000.0 * fps_) : 0;
    }

    QUrl resolvedUrl() const { return url_; }

    // SEQUENTIAL seeks reusing load()'s pipeline; the first failure aborts. No other load() until `done`.
    void extractFrames(const QString& src, const QList<int>& indices,
                       std::function<void(QList<QImage> frames, QString error)> done);

   signals:
    // localPath: the originating LOCAL file, empty for remote images and video frames.
    void loaded(const QImage& image, const QString& localPath);
    void failed(const QString& message);

   private slots:
    void onVideoFrame(const QVideoFrame& frame);

   private:
    void resolve();
    void startVideo(const QUrl& url);
    void tryStartVideoSeek();
    void captureThumbnail();  // read embedded preview/cover art from the player's metadata
    void fail(const QString& message);
    void cleanupVideo();

    QString src_;
    QUrl url_;
    QString localPath_;  // non-empty only for an existing local file
    int frame_ = 0;
    bool done_ = false;     // guards single-shot loaded()/failed()
    bool isVideo_ = false;  // set once the source is resolved as a video
    QImage thumbnail_;      // embedded preview/cover image, if the video carries one
    double fps_ = 0;        // video frame rate (assumed fallback when unknown)
    qint64 durationMs_ = 0; // video duration in ms

    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audio_ = nullptr;
    QVideoSink* sink_ = nullptr;
    QTimer* timeout_ = nullptr;
    bool seekIssued_ = false;
    qint64 targetMs_ = 0;
  };

}

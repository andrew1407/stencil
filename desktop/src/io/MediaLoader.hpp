#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QVideoFrame;
class QTimer;

// Resolves a launch --src (local image, remote URL via fetchGuard, inline data: bytes, or a
// video frame) into one QImage.
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

    // Ranked candidates for ONE picture (a dragged link and the <img> it wrapped). Keeps the
    // first that resolves; failed() carries the FIRST error, since the head is the preferred one.
    void loadFirstOf(const QStringList& sources, int frame);

    // loadFirstOf's list, still in try order; empty for a plain load().
    QStringList rankedSources() const { return candidates; }

    // Valid when loaded() fires.
    bool isVideoSource() const { return isVideo; }

    // Embedded cover/thumbnail (QMediaMetaData), null when absent. Valid when loaded() fires.
    QImage embeddedThumbnail() const { return thumbnail; }

    // Valid when loaded() fires for a video; frameCount() 0 = unknown.
    double frameRate() const { return fps; }
    qint64 getDurationMs() const { return durationMs; }
    int frameCount() const {
      return (fps > 0 && durationMs > 0) ? static_cast<int>(durationMs / 1000.0 * fps) : 0;
    }

    QUrl resolvedUrl() const { return url; }

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
    // The one-source engine behind load() and each loadFirstOf() attempt; leaves the candidate
    // list alone, so a retry keeps its place in it.
    void beginLoad(const QString& src, int frame);
    bool hasMoreCandidates() const { return candidateIndex + 1 < candidates.size(); }
    void resolve();
    void startVideo(const QUrl& url);
    void tryStartVideoSeek();
    void captureThumbnail();  // read embedded preview/cover art from the player's metadata
    void fail(const QString& message);
    void cleanupVideo();

    QString src;
    QStringList candidates;  // loadFirstOf's ranked list; empty for a plain load()
    int candidateIndex = 0;
    QString firstError;      // what the PREFERRED candidate said, reported when all have failed
    QUrl url;
    QString localPath;  // non-empty only for an existing local file
    int frame = 0;
    bool done = false;     // guards single-shot loaded()/failed()
    bool isVideo = false;  // set once the source is resolved as a video
    QImage thumbnail;      // embedded preview/cover image, if the video carries one
    double fps = 0;        // video frame rate (assumed fallback when unknown)
    qint64 durationMs = 0; // video duration in ms

    QMediaPlayer* player = nullptr;
    QAudioOutput* audio = nullptr;
    QVideoSink* sink = nullptr;
    QTimer* timeout = nullptr;
    bool seekIssued = false;
    qint64 targetMs = 0;
  };

}

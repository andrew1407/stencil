#pragma once
#include <QByteArrayView>
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
class QImageReader;

namespace stencil::gui {

  // Pure suffix sniffers off the canon (common/config/mediaTypes.json `surfaces.desktop`).
  bool isVideoFileName(const QString& path);
  bool isImageFileName(const QString& path);

  // A still image's leading bytes, read without decoding. `format` is the signature alone (png,
  // gif, bmp, jpeg, webp; "" for anything else); the size is 0x0 where the header holds none.
  struct ImageHeader {
    QString format;
    quint32 width = 0;
    quint32 height = 0;
  };
  ImageHeader sniffImageHeader(QByteArrayView head);

  // A decode held to LIMITS.imageMaxSide / imageMaxPixels (constants.json), as every surface holds
  // it: a header over the cap is refused before any pixel is allocated (`*tooLarge` set), and Qt's
  // own allocation limit is lifted to fit the cap, so a 16384-px picture decodes as it does elsewhere.
  QImage decodeCapped(QIODevice* dev, bool* tooLarge = nullptr);
  QImage decodeCapped(const QByteArray& bytes, bool* tooLarge = nullptr);
  QImage decodeCappedFile(const QString& path, bool* tooLarge = nullptr);
  // The same cap on a reader its caller drives (a scaled thumbnail decode): false when the header is
  // over it, else Qt's allocation limit is lifted to fit it.
  bool capReader(QImageReader& reader);
  QString imageTooLargeMessage();

  class MediaLoader : public QObject {
    Q_OBJECT
   public:
    explicit MediaLoader(QObject* parent = nullptr);
    ~MediaLoader() override;

    // `frame` is the 0-based video frame. Emits loaded() or failed() exactly once — loaded() always
    // from the event loop, as every decode runs on the pool; a new load() cancels.
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

    // A source a link named is fetched strict (fetchGuard: loopback refused too); a typed one is not.
    // Applies from the next load() on.
    void setStrictFetch(bool on) { strictFetch = on; }

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
    // A remote stream only once the guard, DNS included, has passed its host: Qt's FFmpeg backend
    // resolves and connects on its own, past any check the image fetch makes.
    void startRemoteVideo(const QUrl& url, const QString& fetchError);
    void fetchRemoteImage(const QUrl& url);
    void tryStartVideoSeek();
    void captureThumbnail();  // read embedded preview/cover art from the player's metadata
    void fail(const QString& message);
    void cleanupVideo();
    // `work` decodes on the pool; `then` runs here, and only if no newer load() began meanwhile.
    void decodeThen(std::function<QImage()> work, std::function<void(const QImage&)> then);

    QString src;
    QStringList candidates;  // loadFirstOf's ranked list; empty for a plain load()
    int candidateIndex = 0;
    QString firstError;      // what the PREFERRED candidate said, reported when all have failed
    QUrl url;
    QString localPath;  // non-empty only for an existing local file
    int frame = 0;
    bool strictFetch = false;
    bool done = false;     // guards single-shot loaded()/failed()
    quint64 loadSerial = 0;  // bumped per load, so a decode finishing late cannot answer a newer one
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

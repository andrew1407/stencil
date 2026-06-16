#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QVideoFrame;
class QTimer;

// Resolves a launch --src into a single QImage, asynchronously. It unifies three
// open paths behind one signal:
//   • a local image file            (QImage::load)
//   • a remote image URL            (Qt Network download → QImage)
//   • a video file or direct URL    (Qt Multimedia: seek to a frame and grab it)
// This is the desktop equivalent of the extension's video-frame capture +
// fetch-and-open behavior, driven from the command line instead of the page.
namespace stencil::gui {

  class MediaLoader : public QObject {
    Q_OBJECT
   public:
    explicit MediaLoader(QObject* parent = nullptr);
    ~MediaLoader() override;

    // Begin resolving `src` (path or URL). `frame` is the 0-based video frame to
    // grab (ignored for still images). Emits loaded() or failed() exactly once.
    // Calling load() again cancels any in-flight resolution.
    void load(const QString& src, int frame);

   signals:
    // image  : the decoded pixels.
    // localPath : the originating file path when `src` was a LOCAL image file
    //             (so the editor can keep it for session/project saves); empty
    //             for remote images and video frames (no on-disk original).
    void loaded(const QImage& image, const QString& localPath);
    void failed(const QString& message);

   private slots:
    void onVideoFrame(const QVideoFrame& frame);

   private:
    void resolve();
    void startVideo(const QUrl& url);
    void tryStartVideoSeek();
    void fail(const QString& message);
    void cleanupVideo();

    QString src_;
    QUrl url_;
    QString localPath_;  // non-empty only for an existing local file
    int frame_ = 0;
    bool done_ = false;  // guards single-shot loaded()/failed()

    // Video pipeline (lazily constructed in startVideo()).
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audio_ = nullptr;
    QVideoSink* sink_ = nullptr;
    QTimer* timeout_ = nullptr;
    bool seekIssued_ = false;
    qint64 targetMs_ = 0;
  };

  // Minimal one-shot HTTP(S) GET helper, shared by MediaLoader (remote images)
  // and the launch --layout URL path. The reply + its manager are owned by
  // `owner` and cleaned up after the callback fires.
  namespace net {
    void fetch(QObject* owner, const QUrl& url,
               std::function<void(const QByteArray&)> onOk,
               std::function<void(const QString&)> onErr);
  }

}

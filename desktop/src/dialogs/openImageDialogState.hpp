#pragma once
// The Open Image dialog's state, held by value in bags so the moc'd header stays declarations.
#include <QColor>
#include <QSet>
#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QToolButton;
class QVariantAnimation;
class QVideoSink;
class QWidget;

namespace stencil::gui {

  // The persistent player a video is scrubbed with, loaded once per source.
  struct ScrubPlayer {
    QMediaPlayer* player = nullptr;
    QAudioOutput* audio = nullptr;
    QVideoSink* sink = nullptr;
    double fps = 30.0;
    qint64 durationMs = 0;
    qint64 targetMs = 0;
    bool pending = false;         // awaiting a rendered frame at the seek target
  };

  // The window's own height ease: what it is measured from, and where a flight starts.
  struct HeightEase {
    QScrollArea* bodyScroll = nullptr;  // the body scrolls once the window hits the screen
    QWidget* bodyContent = nullptr;     // …and THIS is what the window is sized from
    QVariantAnimation* anim = nullptr;  // the one running height ease
    int previewCapH = 0;         // the box height the picture gave up to (0 = none)
    int previewFitWidth = 0;     // last width previewFitBox() was computed for (resizeEvent guard)
    bool refitPending = false;   // one coalesced refitWindowHeight() per settle
    int floorH = 0, shownH = 0;  // first-show floor; the height a flight starts from
  };

  // One clock per row that forms and falls, plus the preview cloud's own bookkeeping.
  struct RowMotion {
    QVariantAnimation* dimsAnim = nullptr;         // the read-out's slide
    bool dimsShown = false, quietCrop = false;     // where it is headed; a silent restore
    QVariantAnimation* sizeRowAnim = nullptr;      // the page-size row's own slide
    bool sizeRowShown = false;
    bool albumShown = false;   // the Album/Portrait button's own particle-shown state
    QVariantAnimation* sizeCustomAnim = nullptr;   // the Custom W×H group's own slide
    bool sizeCustomShown = false;
    bool restoring = false;    // a cached re-show, which must not re-key the cache
    int gen = 0;               // bumps on a cancel; a queued raise checks it
    qint64 scatterEnds = 0;    // when the departing picture's cloud lands (ms epoch)
    bool arrivalDue = true;    // a picture blew away; the next one must fly in
    // Every source EVER dusted — a set, not one "last", or file->url->file replayed it.
    QSet<QString> animatedSources;
  };

  // Blank controls: the White/Black presets and the picker all write `color`.
  struct BlankControls {
    QToolButton* swatch = nullptr;
    QSpinBox* width = nullptr;
    QSpinBox* height = nullptr;
    QColor color{Qt::white};
  };

  // The footer's outcomes, and the replace options that ride with one of them.
  struct OpenActions {
    QPushButton* here = nullptr;
    QPushButton* newWindow = nullptr;
    QPushButton* replace = nullptr;
    QPushButton* createBlank = nullptr;
    QWidget* replaceRow = nullptr;
  };

}  // namespace stencil::gui

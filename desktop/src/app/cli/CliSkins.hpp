#pragma once
// The CLI stage's secret skins, common/config/console/skins.json over the qrc: the word that puts
// one on, its phrase, colours and traits, and the frame clock, hues, pictures and cat walk the
// animated ones move on. Ports of cli/src/app/skin/; browser twin js/ui/cli/skin/.
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace stencil::gui {

  struct CliSkin {
    QString key, word, phrase, phraseOff, wordmark, title, body;
    QString mark = QStringLiteral("S");   // the banner's polyline: S, P or M
    bool wordmarkAccented = false, animated = false, movesLogo = false, paintsRuns = false;
    bool slides = false, paintsCells = false, replacesLetters = false;
    QColor fg, bg, titleFg, titleBg, wash, markColor;
  };

  const QVector<CliSkin>& cliSkins();
  const CliSkin* cliSkinByWord(const QString& word);
  QString cliListWord();   // `/eastereggs`
  QColor cliGold();
  int cliFrameMs();
  QString cliWordmark(const CliSkin* skin);   // the skin's, else S T E N C I L
  // The restyle (cli/src/console/render/ansi/restyle.zig): what the accent paints as under a skin — a
  // cell painter's fg, a picture skin's gold, else itself — and whether the skin re-dresses rows at all.
  QColor cliAccentUnder(const CliSkin* skin, const QColor& accent);
  bool cliRestyles(const CliSkin* skin);
  bool cliSwappable(QChar ch);   // an accent letter becomes a picture: printable ASCII, not a space
  int cliRowKey(const QString& text);   // FNV-1a of a row's text, the key its pictures hash on

  // The frame clock: frame 0 is when the skin went on; sync() says whether it moved.
  struct CliSkinClock {
    qint64 startMs = 0;
    int frame = 0;
    quint32 seed = 0;
    int bulb = 0;
    int bulbDue = 0;
    void start(qint64 nowMs);
    bool sync(qint64 nowMs);
    // The fairy lights' next colour when its turn has come: each bulb stays lit bulbFrames.
    std::optional<QColor> nextBulb();
  };

  // The hue a run-painting skin gives column `col`; the bifrost slides it one column a frame.
  QColor cliHueAt(const CliSkin& skin, int col, int frame);

  // The picture index for the letter at `col` of the row keyed `rowKey`, never `prev` again.
  int cliPictureAt(const CliSkin& skin, int rowKey, int col, int prev, const CliSkinClock& clock);
  QString cliPicture(const CliSkin& skin, int index);

  struct CatPose {
    int col = 0, row = 0, cols = 0;
    QString text;
  };
  // Where the cat is and how it looks this frame inside a width × rows frame.
  CatPose cliCatPose(int width, int rows, const CliSkinClock& clock);

}  // namespace stencil::gui

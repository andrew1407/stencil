#pragma once
// The picture under the CLI stage's status line: the editing picture (lines, tint, crop, as
// rendered) or the original, shown by a click on the image's name, a double-click, or Alt held
// over it (Shift too: magnified) — drawn once per request, no transition. Browser twin js/ui/cli/peek.js.
#include <QImage>
#include <QLabel>
#include <functional>
#include <optional>

namespace stencil::gui {

  class CliPeek : public QLabel {
    Q_OBJECT
   public:
    enum class Kind { EDITING, ORIGINAL };
    CliPeek(QWidget* parent, std::function<QImage()> editing, std::function<QImage()> original);

    // One click: the editing picture, or away; a double-click: the original, or back to editing.
    void toggle(Kind kind);
    // Alt held: a peek that leaves when the key does, unless the picture was shown by a click.
    void hold(Kind kind, bool magnified);
    void release();
    void hideNow();
    std::optional<Kind> shown() const { return kind; }
    bool isMagnified() const { return magnified; }
    // The box the picture fits, from the stage's own size: 40% of its height, twice that magnified.
    void fitTo(int stageWidth, int stageHeight);
    void refresh();   // an edit landed: the picture up redraws as it now is

   private:
    void draw(Kind kind, bool magnified);

    std::function<QImage()> editing;
    std::function<QImage()> original;
    std::optional<Kind> kind;
    bool magnified = false;
    bool held = false;
    QImage picture;
    int boxW = 0, boxH = 0;
  };

}  // namespace stencil::gui

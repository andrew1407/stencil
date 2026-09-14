#pragma once
#include "cropGeometry.hpp"
#include "opPlan.hpp"
#include "pageMetrics.hpp"
#include <QImage>
#include <QPair>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

// Op-plan executor (llm-contract.md §1–2); browser twin: browser/js/llm/opPlan.js executeOpPlan.
namespace stencil::gui {
  class CanvasWidget;
}

namespace stencil::llm {

  /* What a .stc `undo` has to put back (contracts/stc §7). The canvas history holds committed
   * lines only, so crop and filter ride along in a checkpoint the runner keeps per edit. */
  struct EditState {
    bool valid = false;
    core::CropRect crop;
    QString filterMode;
    QString filterTint;
    core::Lines lines;
  };

  class PlanTarget {
   public:
    virtual ~PlanTarget() = default;

    virtual bool hasImage() const = 0;
    // Gates the `frame` op (otherwise a plan-level error, §2).
    virtual bool isVideoInput() const { return false; }
    // Rotated-original px — the space crop rects live in.
    virtual QSize effectiveOriginalSize() const = 0;
    // Working (cropped + rotated) px — layout points are clamped to it.
    virtual QSize workingSize() const { return renderResult().size(); }
    // cm, not orientation-swapped.
    virtual core::PageSize pageCm() const = 0;

    virtual bool applyCropRect(const core::CropRect& rect) = 0;
    virtual void rotateQuarter(bool clockwise) = 0;
    virtual void setImageFilter(const QString& mode, const QString& tintHex) = 0;
    virtual void setLayoutLines(const core::Lines& lines) = 0;
    // Installs `lines` and commits ONE undo step; setLayoutLines replaces AND resets the
    // history, which a scripted edit must never do to the user's stack.
    virtual void commitLayoutLines(const core::Lines& lines) { setLayoutLines(lines); }
    // The checkpoint a .stc `undo` reverts to; false = this surface keeps none.
    virtual bool captureEdit(EditState& out) const { return false; }
    // Puts a checkpoint back: crop, filter and lines together.
    void restoreEdit(const EditState& state);
    // `expr` is already charset- and grammar-checked; "" clears the axis.
    virtual void setFormula(QChar axis, const QString& expr) = 0;
    virtual void setFormulasEnabled(bool on) { Q_UNUSED(on); }
    // `isoName` is the UPPERCASE canonical format ("A4").
    virtual void setPageFormat(const QString& isoName) = 0;
    virtual void setPageCustom(double widthCm, double heightCm);
    // Empty `isoName` keeps the current format; widthCm/heightCm > 0 override it (§2).
    virtual bool newBlank(const QString& color, const QString& isoName, double widthCm,
                          double heightCm, QString* err) = 0;
    // Returns steps actually run (< steps when history runs out), -1 = no history here (plan error).
    virtual int stepHistory(bool redo, int steps);
    virtual bool extractFrames(const QVector<int>& indices, QString* err);
    // Re-opens `spec` at `frame` as the WORKING image — the .stc `@frame` inside a `@source`
    // block (contracts/stc §10), which is not the chat's "add each frame as a project".
    virtual bool openSourceFrame(const QString& spec, int frame, QString* err);

    // §10 editor-settings ops: empty/0/-1 = field absent, leave alone. Defaults no-op;
    // connect/disconnect default to a typed failure.
    virtual void setTheme(const QString& mode) { Q_UNUSED(mode); }
    virtual void setAccent(const QString& hex) { Q_UNUSED(hex); }
    // Non-empty *note = skipped (unknown preset), never a plan error.
    virtual void setAccentPreset(const QString& preset, QString* note);
    // fillColor is a BROWSER control — the executor notes+skips it before this is called.
    virtual void setDefaultLineStyle(const Action& a);
    virtual void setUnits(const QString& value) { Q_UNUSED(value); }
    virtual void setViewVisibility(int points, int lines);
    virtual void clearImage() {}
    virtual bool connectServer(const QString& server, QString* err);
    virtual bool disconnectServer(const QString& server, QString* err);
    // Runs only with a working image present; no-image is the executor's note+skip.
    virtual bool copyImage(QString* err);
    virtual bool copyLayout(QString* err);
    virtual bool hasDrawnLines() const { return false; }
    // View-only; false + *err = the surface has no such view (a plan error).
    virtual bool setCompare(const QString& mode, double split, QString* err);
    virtual bool setZoom(int percent, bool fit, QString* err);
    // Three-valued: true + non-empty *note = nothing removed (the plan continues); false = the
    // surface cannot manage projects (*note is the plan error).
    virtual bool removeProjectNamed(const QString& name, bool current, QString* note);
    virtual bool clearProjects(bool keepCurrent, QString* note);
    // Same true+note contract as removeProjectNamed.
    virtual bool renameActiveProject(const QString& name, QString* note);
    virtual bool setProjectColor(const QString& color, QString* note);
    virtual bool setBlankColor(const QString& color, QString* note);
    // `last` = the most recently edited saved project; the model never sees the list.
    virtual bool openProjectNamed(const QString& name, bool last, QString* note);
    virtual bool setIncognito(bool on, QString* note);
    // `open` -1 = leave visibility alone; `dock` "" = leave it where it is.
    virtual bool setChatPlacement(int open, const QString& dock, QString* note);
    // Deferred to the plan's end by executePlan — the dialogs are modal.
    virtual bool openDialog(const QString& name, QString* note);
    // Called LAST by the executor; true + non-empty *note = declined confirm.
    virtual bool clearChat(QString* note);
    // USER-GIVEN URL only: the executor guards the echo rule via userTypedText().
    virtual bool openUrl(const QString& url, bool incognito, QString* err);
    // USER-GIVEN path only (image/video, .json layout, .stencil project); same echo rule.
    virtual bool openFile(const QString& path, QString* err);
    // The only pool openUrl/openFile may echo from; empty blocks both.
    virtual QString userTypedText() const { return QString(); }

    // §2.1 multi-image ops (top-level only). A missing attachment costs the ACTION, not the plan.
    virtual bool loadAttachment(int index, QString* err);
    // `dest` (§10) is a USER-NAMED path — the executor checks the echo rule first.
    virtual bool saveProject(const QString& name, const QString& dest, QString* err);

    // Native-resolution render (filtered image + lines): feeds sandboxes and previews.
    virtual QImage renderResult() const = 0;
  };

  // §10 filesystem guard: `typed` must name `path` or a folder it sits in, written as a path of
  // its own; `..` voids the grant. Twin of the cli's llm.pathEchoedByUser.
  bool pathEchoedIn(const QString& typed, const QString& path);

  // PlanTarget over an offscreen CanvasWidget: variant sandboxes and the headless test.
  class CanvasPlanTarget : public PlanTarget {
   public:
    CanvasPlanTarget(const QImage& image, const core::PageSize& pageCm);
    ~CanvasPlanTarget() override;

    bool hasImage() const override;
    QSize effectiveOriginalSize() const override;
    QSize workingSize() const override;
    core::PageSize pageCm() const override { return page_; }
    bool applyCropRect(const core::CropRect& rect) override;
    void rotateQuarter(bool clockwise) override;
    void setImageFilter(const QString& mode, const QString& tintHex) override;
    void setLayoutLines(const core::Lines& lines) override;
    void commitLayoutLines(const core::Lines& lines) override;
    bool captureEdit(EditState& out) const override;
    void setFormula(QChar axis, const QString& expr) override;
    void setFormulasEnabled(bool on) override { formulasEnabled = on ? 1 : 0; }
    void setPageFormat(const QString& isoName) override;
    void setPageCustom(double widthCm, double heightCm) override;
    bool newBlank(const QString& color, const QString& isoName, double widthCm,
                  double heightCm, QString* err) override;
    int stepHistory(bool redo, int steps) override;
    QImage renderResult() const override;

    void setTheme(const QString& mode) override { themeMode = mode; }
    void setAccent(const QString& hex) override { accentColor = hex; }
    void setAccentPreset(const QString& preset, QString*) override {
      accentPreset = preset;
    }
    void setDefaultLineStyle(const Action& a) override;
    void setUnits(const QString& value) override { unitsValue = value; }
    void setViewVisibility(int points, int lines) override;
    void clearImage() override { cleared = true; }
    bool copyImage(QString*) override { copied = true; return true; }
    bool copyLayout(QString*) override { copiedLayout = true; return true; }
    bool hasDrawnLines() const override;
    bool setCompare(const QString& mode, double split, QString* err) override;
    bool setZoom(int percent, bool fit, QString* err) override;
    // Valid only on a canvas this target created via `blank`; otherwise note+skip.
    bool setBlankColor(const QString& color, QString* note) override;
    bool clearChat(QString*) override { chatCleared = true; return true; }

    // Recorded non-pixel settings ("" / 0 / -1 = never set).
    QString pageFormat;
    double pageCustomW = 0, pageCustomH = 0;
    QString formulaX, formulaY;
    int formulasEnabled = -1;
    QString themeMode, accentColor, accentPreset, unitsValue;
    QString lsColor, lsStyle, lsPointColor, lsDrawMode;
    bool lsPointColorSet = false;
    int lsThickness = 0, lsPointSize = 0;
    int viewPoints = -1, viewLines = -1;
    QString compareMode;
    double compareSplit = 0;
    int zoomPercent = 0;
    bool zoomFit = false;
    bool cleared = false;
    bool chatCleared = false;
    bool copied = false;
    bool copiedLayout = false;

   private:
    std::unique_ptr<gui::CanvasWidget> canvas_;
    core::PageSize page_;
    bool blank_ = false;         // the canvas holds a `blank`-created page
  };

  struct ExecResult {
    bool ok = false;
    QString error;
    bool changed = false;
    // Skipped without failing the plan (§2.1); browser twin: executeOpPlan's `notes`.
    QStringList notes;
    QVector<QPair<QString, QImage>> variants;
  };

  // Plan coordinates are in the frame of the image the model SAW (§1): layout points are
  // re-mapped through earlier crop/rotate actions and clamped. Stops at the first failing action.
  ExecResult executePlan(const OpPlan& plan, PlanTarget& target);

  // Exact URL match first (case-insensitive), else a UNIQUE host match; "" = unknown server.
  QString resolveServerRef(const QString& ref, const QStringList& saved);

}  // namespace stencil::llm

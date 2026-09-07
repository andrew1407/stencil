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

// Op-plan executor (llm-contract.md §1–2 semantics): every op maps onto an
// operation Stencil already has — the SAME code paths the toolbar / crop dialog
// / CLI flags use, reached through the narrow PlanTarget interface so tests can
// drive it with a fixture image (tests/llmExecutor.headless.cpp) and MainWindow
// with the live editor. Top-level actions mutate the working image in order;
// each variant branches from the state AFTER those actions (a sandbox canvas
// seeded with the rendered snapshot), applies its own actions, and yields one
// separate output image.
namespace stencil::gui {
  class CanvasWidget;
}

namespace stencil::llm {

  // The narrow editor surface the executor drives.
  class PlanTarget {
   public:
    virtual ~PlanTarget() = default;

    virtual bool hasImage() const = 0;
    // True when the current input is a video — gates the `frame` op (otherwise
    // a plan-level error, contract §2).
    virtual bool isVideoInput() const { return false; }
    // Rotated-original dimensions in px (the pixel space crop rects live in).
    virtual QSize effectiveOriginalSize() const = 0;
    // Working (cropped + rotated) image dimensions in px — the frame layout
    // points are drawn in and clamped to. Concrete targets override cheaply.
    virtual QSize workingSize() const { return renderResult().size(); }
    // Natural page dimensions (cm, not orientation-swapped) for crop-spec
    // resolution and blank sizing.
    virtual core::PageSize pageCm() const = 0;

    virtual bool applyCropRect(const core::CropRect& rect) = 0;
    virtual void rotateQuarter(bool clockwise) = 0;
    virtual void setImageFilter(const QString& mode, const QString& tintHex) = 0;
    // Adopt the layout lines (image-pixel coordinates), replacing the current
    // layout — the same semantics as applyPastedLayout / stencil.layout.
    virtual void setLayoutLines(const core::Lines& lines) = 0;
    // `expr` is already charset-checked (opPlan) and grammar-checked against the
    // core formula engine (executor) before this is called ("" clears the axis).
    virtual void setFormula(QChar axis, const QString& expr) = 0;
    // §2 formula `enabled` form: switch formulas ON/OFF entirely (the editors'
    // allow-formulas toggle). Default no-op.
    virtual void setFormulasEnabled(bool on) { Q_UNUSED(on); }
    // `isoName` is the UPPERCASE canonical format ("A4").
    virtual void setPageFormat(const QString& isoName) = 0;
    // §2 page custom dims (cm, both > 0) — the editors' custom page size.
    virtual void setPageCustom(double widthCm, double heightCm);
    // New blank page; `isoName` may be empty (keep the current page format);
    // widthCm/heightCm > 0 override it with explicit centimetre dims (§2).
    virtual bool newBlank(const QString& color, const QString& isoName, double widthCm,
                          double heightCm, QString* err) = 0;
    // §2 undo/redo: step this surface's OWN edit history. Returns how many
    // steps actually ran (< steps when history runs out — the executor notes
    // it), or -1 when the surface has no history here (a plan error).
    virtual int stepHistory(bool redo, int steps);
    // Video frame extraction; only the live MainWindow target implements it.
    virtual bool extractFrames(const QVector<int>& indices, QString* err);

    // ── §10 editor-settings ops (GUI editors; parse-banned inside variants).
    // Empty/0/-1 arguments mean "field absent — leave alone". Defaults no-op
    // (CanvasPlanTarget records them for tests); connect/disconnect default to
    // a typed failure — only the live MainWindow target reaches the stores.
    virtual void setTheme(const QString& mode) { Q_UNUSED(mode); }
    virtual void setAccent(const QString& hex) { Q_UNUSED(hex); }
    // §10 accent's preset form: apply a NAMED preset (persists + syncs). A
    // non-empty *note = skipped (unknown preset name) — never a plan error.
    virtual void setAccentPreset(const QString& preset, QString* note);
    // §10 lineStyle: the whole action rides through (color/thickness/pointSize/
    // style plus the widened pointColor/drawMode). fillColor is a BROWSER
    // control — the EXECUTOR notes+skips it before this is called.
    virtual void setDefaultLineStyle(const Action& a);
    virtual void setUnits(const QString& value) { Q_UNUSED(value); }
    virtual void setViewVisibility(int points, int lines);
    // §10 clear: drop the working image and its lines, leaving the editor empty.
    virtual void clearImage() {}
    virtual bool connectServer(const QString& server, QString* err);
    virtual bool disconnectServer(const QString& server, QString* err);
    // §10 copy: the rendered image to the system clipboard (the editors'
    // existing copy-image control). The no-image case is the executor's
    // note+skip; this runs only with a working image present.
    virtual bool copyImage(QString* err);
    // §10 copy what:"layout": the layout JSON instead (the editors' copy-layout
    // control). The no-drawn-lines case is the executor's note+skip.
    virtual bool copyLayout(QString* err);
    // Are there drawn lines to copy? Gates copy what:"layout" (note+skip).
    virtual bool hasDrawnLines() const { return false; }
    // §10 compare / zoom: view-only controls (the exported image is unchanged).
    // false + *err = the surface has no such view (a plan error).
    virtual bool setCompare(const QString& mode, double split, QString* err);
    virtual bool setZoom(int percent, bool fit, QString* err);
    // §10 project management: run the surface's EXISTING remove flows,
    // confirmation dialogs included. true + non-empty *note = nothing was
    // removed (unknown/ambiguous name, declined confirm, empty store) — the
    // executor surfaces the note and the plan continues. false = the surface
    // cannot manage projects at all (*note is the plan error). `current` names
    // the ACTIVE project instead of resolving `name` (§10 removeProject).
    virtual bool removeProjectNamed(const QString& name, bool current, QString* note);
    // §10 clearProjects: `keepCurrent` spares the project open right now.
    virtual bool clearProjects(bool keepCurrent, QString* note);
    // §10 renameProject / projectColor / blankColor / openProject / incognito:
    // the same true+note contract as removeProjectNamed — a non-empty *note
    // (no active project, non-blank editor, unknown name, declined confirm)
    // is surfaced and the plan continues; false = not available here at all.
    virtual bool renameActiveProject(const QString& name, QString* note);
    virtual bool setProjectColor(const QString& color, QString* note);
    virtual bool setBlankColor(const QString& color, QString* note);
    // §10 openProject: `last` = the most recently edited saved project (name is
    // empty then) — the surface resolves it; the model never sees the list.
    virtual bool openProjectNamed(const QString& name, bool last, QString* note);
    virtual bool setIncognito(bool on, QString* note);
    // §10 chatPanel: place the assistant panel itself — `open` -1 = leave the
    // visibility alone, else 0/1; `dock` "" = leave it where it is. Same
    // true+note contract as the ops above.
    virtual bool setChatPlacement(int open, const QString& dock, QString* note);
    // §10 dialog: put one of the editor's own windows in front of the user
    // (projects|servers|shortcuts|visuals|help); an empty `name` closes the open one.
    // Deferred to the plan's end by executePlan — the dialogs are modal.
    virtual bool openDialog(const QString& name, QString* note);
    // §10 clearChat: run the surface's clear-conversation flow, confirm
    // included — the executor calls this LAST, after the plan's other actions
    // and variants. Same three-valued contract: true + non-empty *note =
    // nothing cleared (declined confirm), false = no conversation here.
    virtual bool clearChat(QString* note);
    // §10 openUrl: load a USER-GIVEN image/video URL ("incognito" adopts the
    // incognito mode). The executor guards the user-echo rule via
    // userTypedText(); only the live MainWindow target can actually load.
    virtual bool openUrl(const QString& url, bool incognito, QString* err);
    // §10 openFile: load a USER-GIVEN local file — an image/video, a `.json`
    // layout drawn onto the current picture, or a `.stencil` project. Guarded by
    // the same userTypedText() echo rule as openUrl; only the live MainWindow
    // target can actually read from disk.
    virtual bool openFile(const QString& path, QString* err);
    // The user's OWN messages this conversation — the only pool openUrl and
    // openFile may echo from. Empty (the default) blocks both.
    virtual QString userTypedText() const { return QString(); }

    // ── §2.1 multi-image ops (top-level only; parse-banned inside variants) ──
    // Switch the working image to the turn's `index`-th attachment (1-based).
    // false + *err = this turn has no such attachment, which costs the ACTION
    // (a plan note), never the whole plan.
    virtual bool loadAttachment(int index, QString* err);
    // Persist the working image + layout as a LOCAL project ("" = derive the
    // name from the attachment being worked on, else the editor's own name).
    // `dest` (§10, "" = the editor's own project store) is a USER-NAMED folder or
    // file path — the executor checks the echo rule before passing it on.
    // false + *err fails the plan; the "nothing loaded" case is a note instead.
    virtual bool saveProject(const QString& name, const QString& dest, QString* err);

    // Native-resolution render of the current result (filtered image + drawn
    // lines) — feeds the variant sandboxes and the final previews.
    virtual QImage renderResult() const = 0;
  };

  // §10 filesystem guard: true when `typed` (the user's OWN messages) names `path`, or names
  // a FOLDER it sits in — asking for "~/Downloads" is how people grant a destination, and the
  // file inside it is then the model's to name. A `..` anywhere voids the grant. The folder
  // must be written as a path of its own, so naming one file never hands over its whole
  // directory. Twin of the cli's llm.pathEchoedByUser.
  bool pathEchoedIn(const QString& typed, const QString& path);

  // Concrete PlanTarget over an offscreen CanvasWidget. Used for the variant
  // sandboxes (seeded with the working image's rendered snapshot) and directly
  // by the headless executor test with a fixture image. Formula/page ops don't
  // change pixels; they are recorded on the fields below so tests can assert
  // them.
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
    void setFormula(QChar axis, const QString& expr) override;
    void setFormulasEnabled(bool on) override { formulasEnabled = on ? 1 : 0; }
    void setPageFormat(const QString& isoName) override;
    void setPageCustom(double widthCm, double heightCm) override;
    bool newBlank(const QString& color, const QString& isoName, double widthCm,
                  double heightCm, QString* err) override;
    int stepHistory(bool redo, int steps) override;
    QImage renderResult() const override;

    // §10 recorders (headless executor assertions; never reached via variants).
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
    // §10 blankColor: valid only on a BLANK canvas (one this target created via
    // `blank`) — otherwise the contract's note+skip. Recolours in place,
    // keeping the drawn lines.
    bool setBlankColor(const QString& color, QString* note) override;
    // §10 clearChat recorder (this sandbox has no conversation of its own).
    bool clearChat(QString*) override { chatCleared = true; return true; }

    // Recorded non-pixel settings (for assertions / MainWindow adoption).
    QString pageFormat;          // last `page` op ("" = untouched)
    double pageCustomW = 0, pageCustomH = 0;  // last custom-dims `page` op
    QString formulaX, formulaY;  // last `formula` ops per axis
    int formulasEnabled = -1;    // last `enabled` form (-1 = never set)
    // §10 recorders ("" / 0 / -1 = never set).
    QString themeMode, accentColor, accentPreset, unitsValue;
    QString lsColor, lsStyle, lsPointColor, lsDrawMode;
    bool lsPointColorSet = false;
    int lsThickness = 0, lsPointSize = 0;
    int viewPoints = -1, viewLines = -1;
    QString compareMode;         // last `compare` op ("" = untouched)
    double compareSplit = 0;
    int zoomPercent = 0;         // last `zoom` op (0 / false = never set)
    bool zoomFit = false;
    bool cleared = false;        // a `clear` op ran
    bool chatCleared = false;    // a deferred `clearChat` reached its hook
    bool copied = false;         // a `copy` op reached the clipboard path
    bool copiedLayout = false;   // a copy what:"layout" reached its path

   private:
    std::unique_ptr<gui::CanvasWidget> canvas_;
    core::PageSize page_;
    bool blank_ = false;         // the canvas holds a `blank`-created page
  };

  struct ExecResult {
    bool ok = false;
    QString error;
    // Any top-level action ran (the working image / settings changed).
    bool changed = false;
    // What an executor SKIPPED without failing the plan (§2.1: an attachment
    // this turn cannot satisfy, a save with nothing loaded) — rendered with the
    // reply. Browser twin: executeOpPlan's `notes`.
    QStringList notes;
    // One rendered image per variant, labelled (sanitized; "variant N" when the
    // plan gave none).
    QVector<QPair<QString, QImage>> variants;
  };

  // Execute a VALIDATED plan (parseOpPlan output). Applies plan.actions to
  // `target` in order, then renders each variant from a sandbox seeded with the
  // post-actions snapshot. Stops at the first failing action. Plan coordinates
  // are in the frame of the image the model SAW (contract §1): the executor
  // re-maps layout points through earlier crop/rotate actions (variants inherit
  // the post-actions mapping) and clamps them into the working image's bounds.
  ExecResult executePlan(const OpPlan& plan, PlanTarget& target);

  // §10 connect/disconnect resolution: `ref` against the user's stored server
  // URLs — exact URL match first (case-insensitive), else a UNIQUE host match.
  // "" when nothing (or more than one host) matches; the caller turns that into
  // the "unknown server" plan error. Pure, so headless-testable.
  QString resolveServerRef(const QString& ref, const QStringList& saved);

}  // namespace stencil::llm

#pragma once

#include "planExecutor.hpp"

#include <QImage>
#include <QSize>
#include <QString>

namespace stencil::gui {

  class MainWindow;
  struct Project;

  // Live-editor PlanTarget: every op goes through the SAME appliers the toolbar / dialogs use, persistence and co-edit pushes included. Friend of MainWindow.
  class ChatPlanTarget : public llm::PlanTarget {
   public:
    explicit ChatPlanTarget(MainWindow& w) : w(w) {}
    bool hasImage() const override;
    bool isVideoInput() const override;
    QSize effectiveOriginalSize() const override;
    QSize workingSize() const override;
    core::PageSize pageCm() const override;
    bool applyCropRect(const core::CropRect& rect) override;
    void rotateQuarter(bool clockwise) override;
    void setImageFilter(const QString& mode, const QString& tintHex) override;
    void setLayoutLines(const core::Lines& lines) override;
    void commitLayoutLines(const core::Lines& lines) override;
    std::optional<core::EditorMemento> captureEdit() const override;
    core::FormulaContext formulaContext() const override;
    void setFormula(QChar axis, const QString& expr) override;
    void setFormulasEnabled(bool on) override;
    void setPageFormat(const QString& isoName) override;
    void setPageCustom(double widthCm, double heightCm) override;
    bool newBlank(const QString& color, const QString& isoName, double widthCm,
                  double heightCm, QString* err) override;
    int stepHistory(bool redo, int steps) override;

    // §10 editor-settings ops: the SAME code paths the settings UI drives
    void setTheme(const QString& mode) override;
    void setAccent(const QString& hex) override;
    void setAccentPreset(const QString& preset, QString* note) override;
    void setDefaultLineStyle(const llm::Action& a) override;
    void setUnits(const QString& value) override;
    void setViewVisibility(int points, int lines) override;
    void clearImage() override;
    bool disconnectServer(const QString& server, QString* err) override;
    // The ops that wait on I/O answer as continuations (PlanAwait), never from a nested loop.
    void connectServerThen(const QString& server, llm::OpDone done) override;
    void extractFramesThen(const QVector<int>& indices, llm::OpDone done) override;
    void openUrlThen(const QString& url, bool incognito, llm::OpDone done) override;
    void openFileThen(const QString& path, llm::OpDone done) override;
    void openSourceFrameThen(const QString& spec, int frame, llm::OpDone done) override;
    bool copyImage(QString*) override;
    bool copyLayout(QString*) override;
    bool hasDrawnLines() const override;
    bool setCompare(const QString& mode, double split, QString*) override;
    bool setZoom(int percent, bool fit, QString*) override;
    bool removeProjectNamed(const QString& name, bool current, QString* note) override;
    bool renameActiveProject(const QString& name, QString* note) override;
    bool setProjectColor(const QString& color, QString* note) override;
    bool setBlankColor(const QString& color, QString* note) override;
    void openProjectNamedThen(const QString& name, bool last, llm::OpDone done) override;
    bool setIncognito(bool on, QString* note) override;
    bool setChatPlacement(int open, const QString& dock, QString* note) override;
    bool openDialog(const QString& name, QString* note) override;
    bool clearProjects(bool keepCurrent, QString* note) override;
    bool clearChat(QString*) override;
    // §2.1 multi-image ops
    bool loadAttachment(int index, QString* err) override;
    bool saveProject(const QString& name, const QString& dest, QString* err) override;
    void saveProjectThen(const QString& name, const QString& dest, llm::OpDone done) override;

    QString userTypedText() const override;
    QImage renderResult() const override;

   private:
    const Project* resolveLocalProject(const QString& name, QString* note) const;

    MainWindow& w;
  };

}  // namespace stencil::gui

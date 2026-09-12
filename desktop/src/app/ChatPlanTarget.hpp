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
    explicit ChatPlanTarget(MainWindow& w) : w_(w) {}
    bool hasImage() const override;
    bool isVideoInput() const override;
    QSize effectiveOriginalSize() const override;
    QSize workingSize() const override;
    core::PageSize pageCm() const override;
    bool applyCropRect(const core::CropRect& rect) override;
    void rotateQuarter(bool clockwise) override;
    void setImageFilter(const QString& mode, const QString& tintHex) override;
    void setLayoutLines(const core::Lines& lines) override;
    void setFormula(QChar axis, const QString& expr) override;
    void setFormulasEnabled(bool on) override;
    void setPageFormat(const QString& isoName) override;
    void setPageCustom(double widthCm, double heightCm) override;
    bool newBlank(const QString& color, const QString& isoName, double widthCm,
                  double heightCm, QString* err) override;
    int stepHistory(bool redo, int steps) override;
    bool extractFrames(const QVector<int>& indices, QString* err) override;

    // §10 editor-settings ops: the SAME code paths the settings UI drives
    void setTheme(const QString& mode) override;
    void setAccent(const QString& hex) override;
    void setAccentPreset(const QString& preset, QString* note) override;
    void setDefaultLineStyle(const llm::Action& a) override;
    void setUnits(const QString& value) override;
    void setViewVisibility(int points, int lines) override;
    void clearImage() override;
    bool connectServer(const QString& server, QString* err) override;
    bool disconnectServer(const QString& server, QString* err) override;
    bool openUrl(const QString& url, bool incognito, QString* err) override;
    bool openFile(const QString& path, QString* err) override;
    bool copyImage(QString*) override;
    bool copyLayout(QString*) override;
    bool hasDrawnLines() const override;
    bool setCompare(const QString& mode, double split, QString*) override;
    bool setZoom(int percent, bool fit, QString*) override;
    bool removeProjectNamed(const QString& name, bool current, QString* note) override;
    bool renameActiveProject(const QString& name, QString* note) override;
    bool setProjectColor(const QString& color, QString* note) override;
    bool setBlankColor(const QString& color, QString* note) override;
    bool openProjectNamed(const QString& name, bool last, QString* note) override;
    bool setIncognito(bool on, QString* note) override;
    bool setChatPlacement(int open, const QString& dock, QString* note) override;
    bool openDialog(const QString& name, QString* note) override;
    bool clearProjects(bool keepCurrent, QString* note) override;
    bool clearChat(QString*) override;
    // §2.1 multi-image ops
    bool loadAttachment(int index, QString* err) override;
    bool saveProject(const QString& name, const QString& dest, QString* err) override;

    QString userTypedText() const override;
    QImage renderResult() const override;

   private:
    const Project* resolveLocalProject(const QString& name, QString* note) const;

    MainWindow& w_;
  };

}  // namespace stencil::gui

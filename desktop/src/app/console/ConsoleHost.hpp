#pragma once
// The window's console part: the console window and the CLI stage, the way they clear, the
// gate CLI mode holds over the window-opening ops, and the flows the command handlers call.
// Browser twin: js/console/shell/gate.js and the facade calls the handlers make.
#include "consoleReply.hpp"
#include <QColor>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

class QTimer;

namespace stencil::gui {

  class CliStage;
  class ConsoleWindow;
  class MainWindow;

  class ConsoleHost {
   public:
    explicit ConsoleHost(MainWindow& w) : w(w) {}

    // The console window: one per window; the trigger toggles it.
    void openConsole();
    void closeConsole();
    bool consoleOpen() const;
    ConsoleWindow* consoleWindow() const;
    // What a logo show asks: the modal or popover closed, the menu gone, fullscreen left.
    void clearWay();
    // What CLI mode asks: clearWay, every side window, the chat, the console and the stage.
    void closeEverything();

    // CLI mode: the stage over the whole window, entered once it is bare.
    void enterCli();
    void leaveCli();
    bool cliModeOn() const;
    CliStage* cliStage() const;
    // The refusal while CLI mode is on, for an op that would open a window or move the chat.
    std::optional<QString> cliGate(const QString& what) const;
    // The stage repaints with the theme and keeps its status line with the canvas.
    void onThemeApplied();
    void onCanvasChanged();

    // The flows: the status line, the edits and the settings words.
    struct Status {
      QString text;
      QString name;    // the image's, painted in `color`
      QColor color;
      bool hasImage = false;
    };
    Status status() const;
    bool hasImage() const;
    double speed() const;
    void runScript(const QString& text, support::ConsoleDone done);
    void rotate(int quarters);
    void flip();
    bool step(bool redo);
    int undoAll();
    QString blank(const QStringList& words);                       // "" = done, else the error
    QStringList format(const QStringList& words, QString* err);
    QString filterLabel() const;
    QString lineColor() const;
    QString setLineColor(const QString& color);
    QString pointColor() const;
    QString setPointColor(const QString& color);
    QString projectColor() const;
    QString setProjectColor(const QString& color);
    void applyLayoutJson(const QString& json, support::ConsoleDone done);
    void save(const QString& path, support::ConsoleDone done);
    void saveLayout();
    QStringList theme(const QString& arg, QString* err);
    QStringList revealSpeed(const QString& arg, QString* err);
    QStringList llm(const QString& key, const QString& value, QString* err);
    QString prompt(const QString& text);
    bool syncToServer() const;
    void setSyncToServer(bool on);

    // The openings: pictures, projects, servers and the browser hand-off.
    void upload(const QString& ref, support::ConsoleDone done);
    void open(const QString& ref, support::ConsoleDone done);
    QString openInNewWindow(const QString& ref);
    QStringList localProjectsRows(bool longForm) const;
    QVector<QStringList> localProjectCells(bool longForm) const;
    void projectsThen(const QString& serverUrl, bool longForm, std::function<void(QStringList)> done);
    QStringList connectionsRows() const;
    void connectThen(const QString& url, const QString& token, std::function<void(QString err)> done);
    QString disconnect(const QString& url);
    void fetchThen(const QString& name, const QString& serverUrl, support::ConsoleDone done);
    void browser(bool incognito, support::ConsoleDone done);

   private:
    // A launch link at /open: true when `ref` was one (opened, or refused for naming nothing this app opens).
    bool openLaunchLink(const QString& ref, const support::ConsoleDone& done);
    void buildWindow();
    void buildStage();
    void coverChrome(bool on);
    void rebuildWindowForSkin();   // the shell is built per skin: a switch rebuilds it with its scrollback kept   // the dock edges and the header mark stand down under the stage, as under a show
    bool windowBare() const;
    QString localProjectId(const QString& ref) const;
    QStringList projectRows(const QStringList& names, bool longForm) const;

    MainWindow& w;
    QPointer<ConsoleWindow> window;
    QPointer<CliStage> stage;
    QTimer* wayPoll = nullptr;
    int wayTries = 0;
  };

}  // namespace stencil::gui

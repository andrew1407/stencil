#pragma once
// "Make a copy" (browser core/project/copy/copyProject.js): the one path the projects menu, the
// canvas menu, the toolbar and the copyProject op take — read the source, name it "<name>-copy(N)"
// (core ProjectsStore::copySuffixName), save it here or on its server, then open it.
#include "copyProjectMenu.hpp"   // support::CopyScope
#include "fileStore.hpp"         // Project

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

namespace stencil::net { class ServerClient; }

namespace stencil::gui {

  class MainWindow;
  class ProjectsDialog;

  enum CopyOpen { COPY_OPEN_NONE, COPY_OPEN_HERE, COPY_OPEN_NEW_WINDOW };

  struct CopyRequest {
    QString id;          // a stored row; empty = the live editor
    QString serverUrl;   // with `id`: a server-only row instead
    QString name;        // a server-only row's name, for the question asked before it is read
    support::CopyScope what = support::COPY_LAYOUT;
    CopyOpen open = COPY_OPEN_NONE;
    bool incognito = false;
    bool local = false;  // a server source: make it on this computer instead of on its server
  };

  // What a copy is taken from, one shape for a stored row, the live editor and a server row
  // (browser copy/source.js); `server` is where it lives, empty for a local one.
  struct CopySource {
    core::ProjectMeta meta;
    QString imagePath;   // an on-disk original; else `bytes`, else `image`
    QByteArray bytes;
    QImage image;
    core::Lines lines;
    core::CropRect crop;
    int quarters = 0;
    QJsonObject layout;  // a server row's own layout (its filter and page), used as it is
    QJsonObject chat;
    QString server;
  };

  class ProjectCopy {
   public:
    explicit ProjectCopy(MainWindow& w) : w(w) {}

    // Every UI entry point: the confirmation, then run(); `done` hears the new id ("" when unsaved)
    // and where it opened. `over` stacks the confirmation on another window (the projects list),
    // so its flight plays on that window rather than under it.
    void offer(const CopyRequest& from, const QRect& closeRect = {},
               std::function<void(QString, CopyOpen)> done = {}, QWidget* over = nullptr);
    // The toolbar's Image button: its three scopes, then offer() for the live editor.
    void showToolbarMenu();
    // The projects window's rows; "Just copy" leaves it up with the new row selected.
    void wireProjectsList(ProjectsDialog& dlg);
    // What a request can honour (browser settleCopyOptions): incognito only when opened and local.
    static CopyRequest settle(CopyRequest req, bool serverSource, QString* note);
    // `done(ok, newId, note)`: newId is empty for an incognito copy, `note` names what was dropped.
    void run(const CopyRequest& req, std::function<void(bool ok, QString newId, QString note)> done = {});
    // Enters incognito on this window with `img` under `layout` (a copy opened unsaved).
    void enterIncognito(const QImage& img, const QJsonObject& layout, const core::ProjectMeta& meta);

   private:
    // ProjectCopySource.cpp
    void readSource(const CopyRequest& req, std::function<void(std::optional<CopySource>, QString)> done);
    void readServerRow(const CopyRequest& req, std::function<void(std::optional<CopySource>, QString)> done);
    CopySource liveSource();
    QString liveName();
    QString sourceName(const CopyRequest& from, QString* server);
    QString copyNameAmong(const std::vector<core::ProjectMeta>& extra, const std::string& name) const;
    // ProjectCopySave.cpp
    QString createLocal(const CopySource& src, support::CopyScope what, const QString& name);
    void createOnServer(const CopySource& src, support::CopyScope what, const QString& name,
                        std::function<void(bool ok, QString newId)> done);
    QJsonObject layoutOf(const CopySource& src, support::CopyScope what, int width, int height) const;
    // ProjectCopyOpen.cpp
    // `then` runs once a copy opened here has landed (at once for every other way).
    void openSaved(CopyOpen open, const QString& server, const QString& id, std::function<void()> then);
    void openIncognito(const CopySource& src, support::CopyScope what, CopyOpen open);
    // Saves what is open before a copy replaces it (SourceOpener::openSourceHere's own step).
    void leaveCurrent();

    MainWindow& w;
  };

  // What a copy carries by scope (browser copy/scope.js): the original always, with its blank colour
  // and provenance; the layout from `layout` up; its own colour, words, expiry and chat for `project`.
  Project scopedCopy(const CopySource& src, support::CopyScope what, const std::string& id,
                     const QString& name, long long now);

}  // namespace stencil::gui

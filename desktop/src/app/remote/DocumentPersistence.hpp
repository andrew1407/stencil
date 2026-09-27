#pragma once
#include "SessionController.hpp"
#include <QByteArray>
#include <functional>

namespace stencil::gui {

  class MainWindow;

  // Where the open document is kept: the session's view save and restore, the saved servers
  // reconnected at boot, the linked .stencil file's Save As / delete / external change, and the
  // baked result uploaded to a server project.
  class DocumentPersistence {
   public:
    explicit DocumentPersistence(MainWindow& w) : w(w) {}

    SessionController::Gates sessionGates() const;
    void restoreSession();
    // The boot restore is still decoding: the session on disk is still the one it restores.
    bool sessionRestorePending() const;
    // Browser storage.js parity; touches only the active project's view fields.
    void scheduleViewSave();
    void saveActiveProjectView();
    void autoConnectServers();
    void uploadServerResult(std::function<void()> done);
    void saveProjectFileAs();
    void deleteProjectFile();   // delete the linked .stencil file from disk (confirm), then unlink
    void applyStencilExternal(const QByteArray& text, bool merge = false);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui

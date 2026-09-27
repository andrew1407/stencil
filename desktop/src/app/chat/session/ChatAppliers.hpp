#pragma once
#include "llmSettings.hpp"
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace stencil::llm {
  class LlmClient;
  class QtLlmTransport;
}

namespace stencil::gui {

  class MainWindow;

  // The window's side of the assistant: its LlmClient and effective settings, where the chat doc
  // is filed, and the live-editor appliers a plan's frame / save / open / load ops run through.
  class ChatAppliers {
   public:
    // Out of line, so the unique_ptr of the forward-declared client sees a complete type.
    explicit ChatAppliers(MainWindow& w);
    ~ChatAppliers();

    void ensureLlmClient();
    stencil::llm::LlmSettings currentLlmSettings() const;
    void persistActiveChat();
    void clearPersistedChat();
    void offerChatVideoUpload(const QString& path);
    // Sequential MediaLoader seeks; `done` answers once the last lands, from the event loop.
    void chatExtractFramesThen(const QVector<int>& indices,
                               std::function<void(bool ok, const QString& err)> done);
    bool chatSaveProject(const QString& name, const QString& dest, QString* err);
    void chatSaveProjectThen(const QString& name, const QString& dest,
                             std::function<void(bool ok, const QString& err)> done);
    QString chatSavePath(const QString& name, const QString& dest) const;
    QString chatSaveBaseName(const QString& requested) const;
    QString uniqueLocalProjectName(const QString& wanted) const;
    QString addImageProjectEntry(const QImage& img, const QString& baseName,
                                 bool deferRegistrySave = false);

    stencil::llm::QtLlmTransport* llmTransport = nullptr;
    std::unique_ptr<stencil::llm::LlmClient> llmClient;

   private:
    void fileExtractedFrames(const QList<QImage>& frames, const QVector<int>& indices);

    MainWindow& w;
  };

}  // namespace stencil::gui

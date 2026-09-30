// §10 copyProject on the live editor: the canvas menu's and the toolbar's own path (ProjectCopy),
// with what the request could not have (incognito unopened, or on a server copy) and a copy that
// could not be made coming back as the note (browser llm/adapters/project.js copyActiveProject).
#include "ChatPlanTarget.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"

#include <QHash>

namespace stencil::gui {

  void ChatPlanTarget::copyActiveProjectThen(const llm::Action& a, llm::OpDone done) {
    static const QHash<QString, support::CopyScope> SCOPES = {
        {QStringLiteral("image"), support::COPY_IMAGE},
        {QStringLiteral("layout"), support::COPY_LAYOUT},
        {QStringLiteral("project"), support::COPY_PROJECT}};
    static const QHash<QString, CopyOpen> OPENS = {
        {QStringLiteral("here"), COPY_OPEN_HERE}, {QStringLiteral("newtab"), COPY_OPEN_NEW_WINDOW}};
    if (!w.canvas->hasImage()) return done(true, QStringLiteral("there is no image to copy"));
    CopyRequest req;
    req.what = SCOPES.value(a.what, support::COPY_LAYOUT);
    req.open = OPENS.value(a.open, COPY_OPEN_NONE);
    req.incognito = a.incognito;
    req.local = a.local;
    w.parts.projectCopy.run(req, [done](bool ok, QString, QString note) {
      if (ok) return done(true, note);
      done(true, note.isEmpty() ? QStringLiteral("could not make the copy")
                                : QStringLiteral("could not make the copy — %1").arg(note));
    });
  }

}  // namespace stencil::gui

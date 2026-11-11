#pragma once
// A menu row with two meanings (browser core/pointer/clickOrDouble.js): a click runs `single` once
// the double-click interval passes, a second click inside it runs `dbl` instead; Enter runs `single`
// at once. Both run after every watched menu has closed. Parented to the root menu it watches.
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class QAction;
class QMenu;

namespace stencil::support {

  class MenuDoubleClick : public QObject {
   public:
    explicit MenuDoubleClick(QMenu* root);
    // Every submenu holding a two-meaning row is watched too.
    void watch(QMenu* menu);
    void add(QAction* action, std::function<void()> single, std::function<void()> dbl);

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    struct Pair {
      std::function<void()> single, dbl;
    };
    void fire(QAction* action, bool dbl);

    QList<QPointer<QMenu>> menus;
    QHash<QAction*, Pair> pairs;
    QPointer<QAction> pending;
    QTimer timer;
  };

}  // namespace stencil::support

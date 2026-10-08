// The cells half of the colour drag (colorDrag.hpp): a table whose cells hold swatches drags from a
// press on a swatch cell, or on a chip button inside one, and takes drops on them. Browser twin: the
// Lines-tab row swatches of browser/js/ui/drag/colorDragSwatches.js.
#include "colorDragParts.hpp"
#include <QAbstractButton>
#include <QChildEvent>
#include <memory>

namespace stencil::support {

  using namespace colorDragParts;

  namespace {
    // A button in a cell takes the presses the viewport never sees: each drags its own cell.
    class CellChips : public QObject {
     public:
      explicit CellChips(QTableWidget* table) : QObject(table->viewport()), table(table) {}
      void adopt(QWidget* w) {
        QList<QAbstractButton*> buttons = w->findChildren<QAbstractButton*>();
        if (auto* b = qobject_cast<QAbstractButton*>(w)) buttons << b;
        for (QAbstractButton* b : buttons)
          installIconDrag(b, colorHooks([view = table.data(), port = table->viewport(), b] {
            const QModelIndex idx = view->indexAt(b->mapTo(port, b->rect().center()));
            return cellSpot(tables().value(port), idx.row(), idx.column());
          }, b));
      }

     protected:
      bool eventFilter(QObject*, QEvent* ev) override {
        if (ev->type() == QEvent::ChildAdded)
          if (auto* w = qobject_cast<QWidget*>(static_cast<QChildEvent*>(ev)->child())) adopt(w);
        return false;
      }

     private:
      QPointer<QTableWidget> table;
    };
  }  // namespace

  void installColorDragCells(QTableWidget* table, std::function<ColorSwatch(int row, int column)> at) {
    if (!table) return;
    QWidget* port = table->viewport();
    tables().insert(port, Cells{table, std::move(at)});
    QObject::connect(port, &QObject::destroyed, [port] { tables().remove(port); });
    auto pressed = std::make_shared<std::optional<Spot>>();
    IconDragHooks h = colorHooks([pressed] { return *pressed; }, port);
    h.grab = [table, port, pressed](const QPoint& global) {
      const QModelIndex idx = table->indexAt(port->mapFromGlobal(global));
      *pressed = cellSpot(tables().value(port), idx.row(), idx.column());
      if (!*pressed) return QRect();
      const QRect cell = table->visualRect(idx);
      return QRect(port->mapToGlobal(cell.topLeft()), cell.size());
    };
    installIconDrag(port, std::move(h));
    auto* chipsIn = new CellChips(table);
    port->installEventFilter(chipsIn);
    for (QWidget* cell : port->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) chipsIn->adopt(cell);
  }

}  // namespace stencil::support

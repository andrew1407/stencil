#pragma once
#include <QEvent>
#include <QFrame>
#include <QResizeEvent>
#include <QToolBar>
#include <QWidget>

#include "flowLayout.hpp"

// A toolbar row that WRAPS instead of pushing what does not fit into QToolBar's "»", where
// a widget action is simply gone. The row holds one expanding child on a FlowLayout
// (browser parity: flex-wrap); QToolBarLayout ignores heightForWidth, so it pins its own.
namespace stencil::gui {

  class WrapRow : public QWidget {
   public:
    explicit WrapRow(QWidget* parent) : QWidget(parent) {
      setObjectName("toolWrapRow");
      flow_ = new FlowLayout(this, 0, 6, 4);
      setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    FlowLayout* flow() const { return flow_; }
    void add(QWidget* w) { flow_->addWidget(w); }
    // A hidden row gets no resize, so its pinned height is stale until asked.
    void remeasure() { sync(); }

   protected:
    void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e); sync(); }
    void showEvent(QShowEvent* e) override { QWidget::showEvent(e); sync(); }
    // A section shown or hidden re-flows the row without changing its width.
    bool event(QEvent* e) override {
      const bool r = QWidget::event(e);
      if (e->type() == QEvent::LayoutRequest) sync();
      return r;
    }

   private:
    // Converges: the new height feeds back as one more resize at the same width.
    void sync() {
      if (width() <= 0) return;
      const int h = flow_->heightForWidth(width());
      if (h > 0 && h != height()) setFixedHeight(h);
    }
    FlowLayout* flow_ = nullptr;
  };

  // The hairline between two sections, stretched to its line's height (browser .ctrl-sep
  // align-self: stretch) — a wrapped line's height is not known until the flow places it.
  inline QWidget* makeWrapSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName("toolWrapSep");
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedWidth(1);
    line->setMinimumHeight(20);   // the floor a one-control line would give it
    line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return line;
  }

  // The row a toolbar already has, if any — never creates one.
  inline WrapRow* wrapRowIn(QToolBar* tb) {
    if (!tb) return nullptr;
    for (QObject* c : tb->children())
      if (c->objectName() == QLatin1String("toolWrapRow"))
        if (auto* found = dynamic_cast<WrapRow*>(c)) return found;
    return nullptr;
  }

  // The row's wrapping host, created on first use and kept as the toolbar's only item.
  inline WrapRow* wrapRowFor(QToolBar* tb) {
    if (WrapRow* had = wrapRowIn(tb)) return had;   // no Q_OBJECT here: named, then dynamic_cast
    auto* row = new WrapRow(tb);
    tb->addWidget(row);
    return row;
  }
  inline void addWrapped(QToolBar* tb, QWidget* w) { wrapRowFor(tb)->add(w); }
  inline void addWrappedSeparator(QToolBar* tb) {
    auto* row = wrapRowFor(tb);
    row->add(makeWrapSeparator(row));
  }

}  // namespace stencil::gui

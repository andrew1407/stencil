#pragma once
#include <QEvent>
#include <QFrame>
#include <QResizeEvent>
#include <QToolBar>
#include <QWidget>

#include "FlowLayout.hpp"

// A toolbar row that WRAPS (browser flex-wrap) instead of overflowing into QToolBar's
// "»". QToolBarLayout ignores heightForWidth, so the row pins its own.
namespace stencil::gui {

  class WrapRow : public QWidget {
   public:
    explicit WrapRow(QWidget* parent) : QWidget(parent) {
      setObjectName("toolWrapRow");
      flow = new FlowLayout(this, 0, 6, 4);
      setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    FlowLayout* getFlow() const { return flow; }
    void add(QWidget* w) { flow->addWidget(w); }
    // A hidden row gets no resize, so its pinned height is stale until asked.
    void remeasure() { sync(); }

    // Both hints are the WRAPPED height: the stacked first-pass hint becomes the bar's
    // minimum, and the window never gives that height back.
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return flow->heightForWidth(w); }
    QSize sizeHint() const override {
      const int w = measureWidth();
      return w > 0 ? QSize(flow->minimumSize().width(), flow->heightForWidth(w))
                   : QWidget::sizeHint();
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e); sync(); }
    void showEvent(QShowEvent* e) override { QWidget::showEvent(e); sync(); }
    bool event(QEvent* e) override {
      const bool r = QWidget::event(e);
      if (e->type() == QEvent::LayoutRequest) sync();
      return r;
    }

   private:
    // Before layout, fall back to the WINDOW's width — what the toolbar will hand over.
    static constexpr int LAID_OUT = 200;
    int measureWidth() const {
      if (width() > LAID_OUT) return width();
      const QWidget* top = window();
      return top ? top->width() : 0;
    }
    // Converges: the new height feeds back as one more resize at the same width.
    void sync() {
      const int w = measureWidth();
      if (w <= 0) return;
      const int h = flow->heightForWidth(w);
      if (h > 0 && h != height()) setFixedHeight(h);
    }
    FlowLayout* flow = nullptr;
  };

  // Browser .ctrl-sep align-self: stretch — a wrapped line's height is only known after placement.
  inline QWidget* makeWrapSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName("toolWrapSep");
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedWidth(1);
    line->setMinimumHeight(20);   // the floor a one-control line would give it
    line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return line;
  }

  // Never creates one.
  inline WrapRow* wrapRowIn(QToolBar* tb) {
    if (!tb) return nullptr;
    for (QObject* c : tb->children())
      if (c->objectName() == QLatin1String("toolWrapRow"))
        if (auto* found = dynamic_cast<WrapRow*>(c)) return found;
    return nullptr;
  }

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

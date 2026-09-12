#pragma once
// The search combo's popup bounds and filter proxy, private to the SearchCombo*.cpp TUs.
#include <QSortFilterProxyModel>
#include <QString>

namespace stencil::gui {


  // Browser .accent-dd-menu max-height: 280px.
  inline constexpr int MAX_POPUP_HEIGHT = 280;
  // .accent-dd-menu padding: 4px.
  inline constexpr int POPUP_PADDING = 4;

  // rowMatches(label + value, query): substring over the label AND the item DATA, so "a4" matches any unit.
  class LabelValueFilterProxy : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setQuery(const QString& query) {
      query_ = query.trimmed();
      invalidate();  // re-run filterAcceptsRow (portable across Qt 6.x)
    }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
      if (query_.isEmpty()) return true;
      const QModelIndex idx = sourceModel()->index(row, 0, parent);
      const QString hay = idx.data(Qt::DisplayRole).toString() + ' ' +
                          idx.data(Qt::UserRole).toString();
      return hay.contains(query_, Qt::CaseInsensitive);
    }

  private:
    QString query_;
  };


}  // namespace stencil::gui

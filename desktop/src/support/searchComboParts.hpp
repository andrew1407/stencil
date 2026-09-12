#pragma once
// The search combo's popup bounds and filter proxy, private to the searchCombo*.cpp TUs.
#include <QSortFilterProxyModel>
#include <QString>

namespace stencil::gui {


  // Browser parity: .accent-dd-menu caps at max-height: 280px.
  inline constexpr int kMaxPopupHeight = 280;
  // .accent-dd-menu padding: 4px (also the search-row → list gap).
  inline constexpr int kPopupPadding = 4;

  // rowMatches(label + value, query): case-insensitive substring over the
  // display label AND the canonical item DATA ("A4"/"custom"), so "a4"
  // matches whatever unit the label is currently rendered in.
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

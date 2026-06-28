#pragma once
#include "fileStore.hpp"
#include <QDialog>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

// Settings editor. Mirrors browser/js/ui/settingsModal.js (theme, autosave,
// visibility toggles and the default line visuals). Construct with the current
// Settings, exec(); on QDialog::Accepted, read result().
namespace stencil::gui {

  class SettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit SettingsDialog(const Settings& current, QWidget* parent = nullptr);
    Settings result() const;

   private:
    void pickColor();

    Settings base_;  // preserves fields this dialog doesn't edit (formulas etc.)
    QComboBox* theme_ = nullptr;
    QComboBox* accent_ = nullptr;
    QCheckBox* autosave_ = nullptr;
    QCheckBox* syncToServer_ = nullptr;
    QCheckBox* showPoints_ = nullptr;
    QCheckBox* showLines_ = nullptr;
    QPushButton* color_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* markerSize_ = nullptr;
    QComboBox* style_ = nullptr;
    QComboBox* page_ = nullptr;
    QDoubleSpinBox* customW_ = nullptr;
    QDoubleSpinBox* customH_ = nullptr;
    QSpinBox* holdDelay_ = nullptr;
    QString colorHex_;
  };

}

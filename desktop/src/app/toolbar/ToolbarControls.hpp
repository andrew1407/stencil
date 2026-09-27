#pragma once
#include "accentDefaults.hpp"
#include "defaultVisuals.hpp"

#include <QColor>
#include <QSet>
#include <QString>

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTimer;
class QToolBar;
class QToolButton;
class QVariantAnimation;
class QWidget;

namespace stencil::gui {

  // The toolbar rows' own controls. Browser toolbar.js sections; these set canvas DEFAULTS only —
  // selected-line editing is SelectionPanel's.
  struct ToolbarControls {
    QToolButton* drawModeBtn = nullptr;
    QToolButton* zoomFitBtn = nullptr;
    QCheckBox* showPointsCheck = nullptr;
    QCheckBox* showLinesCheck = nullptr;
    QToolButton* startDrawBtn = nullptr;
    QToolButton* lineColorBtn = nullptr;
    QToolButton* pointColorBtn = nullptr;
    QSpinBox* lineThickness = nullptr;
    QSpinBox* pointSize = nullptr;
    QComboBox* lineStyle = nullptr;
    QComboBox* imageFilter = nullptr;
    QComboBox* compareCombo = nullptr;
    QToolButton* filterColorBtn = nullptr;
    QToolBar* styleToolbar = nullptr;
    QWidget* imageSection = nullptr;
    QSet<QAction*> dangerIcons;
    QToolButton* openImageBtn = nullptr;
    QColor lineColorValue{defaultVisuals::table().color}, filterColorValue{DEFAULT_ACCENT_HEX};

    QCheckBox* allowFormulas = nullptr;
    QWidget* formulaGroup = nullptr;
    QLineEdit* formulaX = nullptr;
    QLineEdit* formulaY = nullptr;
    QLabel* formulaError = nullptr;
    // Commits when typing settles, never per keystroke; browser twin settingsController.wireFormulaInputs, same delay.
    QTimer* formulaCommitTimer = nullptr;

    QToolButton* controlsPill = nullptr;
    qreal pillChevronDeg = 0;
    QVariantAnimation* pillSpinAnim = nullptr;
    QToolBar* headerToolbar = nullptr;
    QWidget* settingsSection = nullptr;
    QWidget* connectionsSection = nullptr;
    QToolButton* panelReopenBtn = nullptr;
    QLabel* statusHint = nullptr;
    QAction* statusHintAction = nullptr;

    QLabel *imageSizeInfo = nullptr, *incognitoTag = nullptr;
    QWidget* imageInfoBar = nullptr;
    QWidget* imageInfoHost = nullptr;
    QDockWidget* imageInfoDock = nullptr;
    QString imageInfoHeightKey;
    QWidget* dropHint = nullptr;
    QLabel* dropHintIcon = nullptr;
    QLabel* dropHintText = nullptr;
    QToolButton* logoBtn = nullptr;
    QTimer* logoClickTimer = nullptr;
    bool altHeldForTest = false;
    QWidget* logoFx = nullptr;
  };

}  // namespace stencil::gui

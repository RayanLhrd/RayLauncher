// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayTheme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

namespace {
// Design tokens — keep in sync with the plan at plans/staged-chasing-cupcake.md.
// Changing any of these is a user-visible look change; do it deliberately.
constexpr const char* kBgBase        = "#13181F";  // App root background
constexpr const char* kBgRaised      = "#1E252F";  // Cards, menus, dialogs
constexpr const char* kBgRaisedHover = "#242B36";  // Hover state on raised surfaces
constexpr const char* kBgInput       = "#0E1217";  // Text fields, list view rows
constexpr const char* kBgSubtle      = "#1A2028";  // Alternate row background
constexpr const char* kBorder        = "#2A323F";  // Default subtle border
constexpr const char* kBorderHover   = "#5BC85C";  // Interactive hover border
constexpr const char* kTextPrimary   = "#E6EDF3";  // Main labels
constexpr const char* kTextSecondary = "#8B98A8";  // Descriptions, placeholders
constexpr const char* kAccent        = "#5BC85C";  // Primary action (Install, Play)
constexpr const char* kAccentHover   = "#6FD36D";  // Accent hover
constexpr const char* kWarning       = "#F5A524";  // Update button
constexpr const char* kWarningHover  = "#F8B848";  // Update hover
constexpr const char* kDanger        = "#EF4444";  // Kill, destructive
constexpr const char* kDangerHover   = "#F26666";  // Danger hover
constexpr const char* kInfo          = "#3B82F6";  // Install (download action)
constexpr const char* kInfoHover     = "#60A5FA";  // Install hover

// Single global stylesheet. Placed as a raw-string literal so it keeps its readable formatting.
// Selector order: Qt built-ins first (roughly layout order from root to leaves), then our
// custom widgets at the bottom so their rules shadow the generic ones.
const char* kGlobalStyleSheetTemplate = R"QSS(
    /* ======================================================================
       Root surfaces
       ====================================================================== */
    QMainWindow, QDialog {
        background-color: %BG_BASE%;
        color: %TEXT_PRIMARY%;
    }
    QWidget {
        color: %TEXT_PRIMARY%;
    }

    /* ======================================================================
       Toolbars + menus
       ====================================================================== */
    QToolBar {
        background-color: %BG_BASE%;
        border: none;
        padding: 4px;
        spacing: 4px;
    }
    QToolBar::separator {
        background-color: %BORDER%;
        width: 1px;
        margin: 6px 4px;
    }
    QToolButton {
        background-color: transparent;
        color: %TEXT_PRIMARY%;
        border: 1px solid transparent;
        border-radius: 6px;
        padding: 6px 10px;
    }
    QToolButton:hover {
        background-color: %BG_RAISED_HOVER%;
        border-color: %BORDER%;
    }
    QToolButton:pressed {
        background-color: %BG_INPUT%;
    }
    QToolButton:checked {
        background-color: %BG_RAISED%;
        border-color: %ACCENT%;
    }
    QMenuBar {
        background-color: %BG_BASE%;
        color: %TEXT_PRIMARY%;
        border-bottom: 1px solid %BORDER%;
    }
    QMenuBar::item {
        background: transparent;
        padding: 6px 10px;
    }
    QMenuBar::item:selected {
        background-color: %BG_RAISED_HOVER%;
        border-radius: 4px;
    }
    QMenu {
        background-color: %BG_RAISED%;
        color: %TEXT_PRIMARY%;
        border: 1px solid %BORDER%;
        border-radius: 8px;
        padding: 4px;
    }
    QMenu::item {
        padding: 8px 14px;
        border-radius: 4px;
    }
    QMenu::item:selected {
        background-color: %ACCENT%;
        color: %BG_BASE%;
    }
    QMenu::item:disabled {
        color: %TEXT_SECONDARY%;
    }
    QMenu::separator {
        height: 1px;
        background: %BORDER%;
        margin: 4px 6px;
    }

    /* ======================================================================
       Buttons
       ====================================================================== */
    QPushButton {
        background-color: %BG_RAISED%;
        border: 1px solid %BORDER%;
        border-radius: 8px;
        padding: 8px 16px;
        color: %TEXT_PRIMARY%;
        font-weight: 600;
    }
    QPushButton:hover {
        background-color: %BG_RAISED_HOVER%;
        border-color: %ACCENT%;
    }
    QPushButton:pressed {
        background-color: %BG_INPUT%;
    }
    QPushButton:disabled {
        color: %TEXT_SECONDARY%;
        background-color: %BG_RAISED%;
        border-color: %BORDER%;
    }
    /* Accented primary buttons — the ones marked .default=true programmatically */
    QPushButton:default {
        background-color: %ACCENT%;
        color: %BG_BASE%;
        border-color: %ACCENT%;
    }
    QPushButton:default:hover {
        background-color: %ACCENT_HOVER%;
        border-color: %ACCENT_HOVER%;
    }
    QPushButton:default:pressed {
        background-color: %ACCENT%;
    }
    QPushButton:default:disabled {
        background-color: %BG_RAISED%;
        color: %TEXT_SECONDARY%;
        border-color: %BORDER%;
    }

    /* ======================================================================
       Inputs
       ====================================================================== */
    QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QTimeEdit, QDateEdit, QDateTimeEdit {
        background-color: %BG_INPUT%;
        border: 1px solid %BORDER%;
        border-radius: 6px;
        padding: 6px 10px;
        color: %TEXT_PRIMARY%;
        selection-background-color: %ACCENT%;
        selection-color: %BG_BASE%;
    }
    QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus,
    QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
        border-color: %ACCENT%;
    }
    QLineEdit:disabled, QTextEdit:disabled {
        color: %TEXT_SECONDARY%;
        background-color: %BG_RAISED%;
    }

    QComboBox {
        background-color: %BG_INPUT%;
        border: 1px solid %BORDER%;
        border-radius: 6px;
        padding: 6px 10px;
        color: %TEXT_PRIMARY%;
    }
    QComboBox:hover {
        border-color: %BORDER_HOVER%;
    }
    QComboBox::drop-down {
        border: none;
        width: 20px;
    }
    QComboBox QAbstractItemView {
        background-color: %BG_RAISED%;
        border: 1px solid %BORDER%;
        border-radius: 6px;
        selection-background-color: %ACCENT%;
        selection-color: %BG_BASE%;
        padding: 4px;
        outline: none;
    }

    /* Checkbox / radio — flat tick, no OS native chrome */
    QCheckBox, QRadioButton {
        color: %TEXT_PRIMARY%;
        spacing: 8px;
    }
    QCheckBox::indicator, QRadioButton::indicator {
        width: 16px;
        height: 16px;
        background-color: %BG_INPUT%;
        border: 1px solid %BORDER%;
    }
    QCheckBox::indicator { border-radius: 3px; }
    QRadioButton::indicator { border-radius: 8px; }
    QCheckBox::indicator:hover, QRadioButton::indicator:hover {
        border-color: %BORDER_HOVER%;
    }
    QCheckBox::indicator:checked, QRadioButton::indicator:checked {
        background-color: %ACCENT%;
        border-color: %ACCENT%;
    }

    /* ======================================================================
       Lists / trees / tables
       ====================================================================== */
    QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
        background-color: %BG_BASE%;
        alternate-background-color: %BG_SUBTLE%;
        border: 1px solid %BORDER%;
        border-radius: 8px;
        color: %TEXT_PRIMARY%;
        selection-background-color: %ACCENT%;
        selection-color: %BG_BASE%;
        outline: none;
    }
    QListView::item, QTreeView::item, QTableView::item, QListWidget::item, QTreeWidget::item {
        padding: 6px;
        border-radius: 4px;
    }
    QListView::item:hover, QTreeView::item:hover, QListWidget::item:hover, QTreeWidget::item:hover {
        background-color: %BG_RAISED_HOVER%;
    }
    QHeaderView::section {
        background-color: %BG_RAISED%;
        color: %TEXT_PRIMARY%;
        border: none;
        border-bottom: 1px solid %BORDER%;
        padding: 6px 10px;
        font-weight: 600;
    }

    /* ======================================================================
       Tabs
       ====================================================================== */
    QTabWidget::pane {
        border: 1px solid %BORDER%;
        background-color: %BG_BASE%;
        border-radius: 8px;
        top: -1px;
    }
    QTabBar::tab {
        background-color: transparent;
        color: %TEXT_SECONDARY%;
        padding: 8px 16px;
        border: none;
        border-bottom: 2px solid transparent;
        margin-right: 2px;
    }
    QTabBar::tab:selected {
        color: %TEXT_PRIMARY%;
        border-bottom-color: %ACCENT%;
    }
    QTabBar::tab:hover:!selected {
        color: %TEXT_PRIMARY%;
        border-bottom-color: %BORDER%;
    }

    /* ======================================================================
       Scrollbars
       ====================================================================== */
    QScrollBar:vertical {
        background: transparent;
        width: 10px;
        margin: 0;
    }
    QScrollBar::handle:vertical {
        background: %BORDER%;
        border-radius: 5px;
        min-height: 30px;
    }
    QScrollBar::handle:vertical:hover {
        background: %TEXT_SECONDARY%;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
        height: 0;
    }
    QScrollBar:horizontal {
        background: transparent;
        height: 10px;
    }
    QScrollBar::handle:horizontal {
        background: %BORDER%;
        border-radius: 5px;
        min-width: 30px;
    }
    QScrollBar::handle:horizontal:hover {
        background: %TEXT_SECONDARY%;
    }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
        width: 0;
    }

    /* ======================================================================
       Progress bar
       ====================================================================== */
    QProgressBar {
        background-color: %BG_INPUT%;
        border: 1px solid %BORDER%;
        border-radius: 6px;
        text-align: center;
        color: %TEXT_PRIMARY%;
        min-height: 14px;
    }
    QProgressBar::chunk {
        background-color: %ACCENT%;
        border-radius: 5px;
    }

    /* ======================================================================
       Groups, frames, status bar, tooltips, labels
       ====================================================================== */
    QGroupBox {
        color: %TEXT_PRIMARY%;
        border: 1px solid %BORDER%;
        border-radius: 8px;
        margin-top: 14px;
        padding: 14px 10px 10px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        padding: 0 8px;
        color: %TEXT_SECONDARY%;
        font-weight: 600;
    }
    QStatusBar {
        background-color: %BG_BASE%;
        color: %TEXT_SECONDARY%;
        border-top: 1px solid %BORDER%;
    }
    QToolTip {
        background-color: %BG_RAISED%;
        color: %TEXT_PRIMARY%;
        border: 1px solid %BORDER%;
        border-radius: 6px;
        padding: 6px 10px;
    }
    QSplitter::handle {
        background-color: %BORDER%;
    }
    QFrame[frameShape="4"], QFrame[frameShape="5"] {
        /* HLine / VLine — make separators match our border color instead of the OS default */
        color: %BORDER%;
        background-color: %BORDER%;
    }

    /* ======================================================================
       Custom RayLauncher widgets
       ====================================================================== */
    RayModpackCard {
        background-color: %BG_RAISED%;
        border: 1px solid %BORDER%;
        border-radius: 10px;
    }
    RayModpackCard:hover {
        background-color: %BG_RAISED_HOVER%;
        border-color: %ACCENT%;
    }
    /* Install = download action → blue, distinct from Play's "go" green. */
    RayModpackCard QPushButton#installButton {
        background-color: %INFO%;
        color: %TEXT_PRIMARY%;
        border-color: %INFO%;
    }
    RayModpackCard QPushButton#installButton:hover {
        background-color: %INFO_HOVER%;
        border-color: %INFO_HOVER%;
    }
    RayModpackCard QPushButton#playButton {
        background-color: %ACCENT%;
        color: %BG_BASE%;
        border-color: %ACCENT%;
    }
    RayModpackCard QPushButton#playButton:hover {
        background-color: %ACCENT_HOVER%;
        border-color: %ACCENT_HOVER%;
    }
    RayModpackCard QPushButton#updateButton {
        background-color: %WARNING%;
        color: %BG_BASE%;
        border-color: %WARNING%;
    }
    RayModpackCard QPushButton#updateButton:hover {
        background-color: %WARNING_HOVER%;
        border-color: %WARNING_HOVER%;
    }
    RayModpackCard QPushButton#killButton {
        background-color: %DANGER%;
        color: %TEXT_PRIMARY%;
        border-color: %DANGER%;
    }
    RayModpackCard QPushButton#killButton:hover {
        background-color: %DANGER_HOVER%;
        border-color: %DANGER_HOVER%;
    }
)QSS";

QString buildStyleSheet()
{
    // Simple token replacement — avoids pulling in a templating lib. Each token appears many
    // times in the QSS so substitution is worth it.
    QString qss = QString::fromLatin1(kGlobalStyleSheetTemplate);
    const QPair<QLatin1String, QLatin1String> tokens[] = {
        { QLatin1String("%BG_BASE%"),         QLatin1String(kBgBase) },
        { QLatin1String("%BG_RAISED_HOVER%"), QLatin1String(kBgRaisedHover) },
        { QLatin1String("%BG_RAISED%"),       QLatin1String(kBgRaised) },
        { QLatin1String("%BG_INPUT%"),        QLatin1String(kBgInput) },
        { QLatin1String("%BG_SUBTLE%"),       QLatin1String(kBgSubtle) },
        { QLatin1String("%BORDER_HOVER%"),    QLatin1String(kBorderHover) },
        { QLatin1String("%BORDER%"),          QLatin1String(kBorder) },
        { QLatin1String("%TEXT_PRIMARY%"),    QLatin1String(kTextPrimary) },
        { QLatin1String("%TEXT_SECONDARY%"),  QLatin1String(kTextSecondary) },
        { QLatin1String("%ACCENT_HOVER%"),    QLatin1String(kAccentHover) },
        { QLatin1String("%ACCENT%"),          QLatin1String(kAccent) },
        { QLatin1String("%WARNING_HOVER%"),   QLatin1String(kWarningHover) },
        { QLatin1String("%WARNING%"),         QLatin1String(kWarning) },
        { QLatin1String("%DANGER_HOVER%"),    QLatin1String(kDangerHover) },
        { QLatin1String("%DANGER%"),          QLatin1String(kDanger) },
        { QLatin1String("%INFO_HOVER%"),      QLatin1String(kInfoHover) },
        { QLatin1String("%INFO%"),            QLatin1String(kInfo) },
    };
    for (const auto& [token, value] : tokens) {
        qss.replace(token, value);
    }
    return qss;
}
}  // namespace

void RayTheme::applyGlobalStyle()
{
    // Fusion gives us a predictable cross-platform base to layer QSS on top of. Native styles
    // ignore a lot of our stylesheet rules.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }

    QPalette pal;
    pal.setColor(QPalette::Window, QColor(kBgBase));
    pal.setColor(QPalette::WindowText, QColor(kTextPrimary));
    pal.setColor(QPalette::Base, QColor(kBgInput));
    pal.setColor(QPalette::AlternateBase, QColor(kBgSubtle));
    pal.setColor(QPalette::ToolTipBase, QColor(kBgRaised));
    pal.setColor(QPalette::ToolTipText, QColor(kTextPrimary));
    pal.setColor(QPalette::PlaceholderText, QColor(kTextSecondary));
    pal.setColor(QPalette::Text, QColor(kTextPrimary));
    pal.setColor(QPalette::Button, QColor(kBgRaised));
    pal.setColor(QPalette::ButtonText, QColor(kTextPrimary));
    pal.setColor(QPalette::BrightText, QColor(kTextPrimary));
    pal.setColor(QPalette::Link, QColor(kAccent));
    pal.setColor(QPalette::LinkVisited, QColor(kAccentHover));
    pal.setColor(QPalette::Highlight, QColor(kAccent));
    pal.setColor(QPalette::HighlightedText, QColor(kBgBase));

    // Disabled variants — muted text so read-only fields look obviously inactive.
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(kTextSecondary));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(kTextSecondary));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(kTextSecondary));
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(kBorder));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(kTextSecondary));

    QApplication::setPalette(pal);
    // setStyleSheet is a non-static slot on QApplication — has to go through the singleton.
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        app->setStyleSheet(buildStyleSheet());
    }
}

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QFrame>
#include <QPoint>
#include <QString>

#include "modplatform/raylauncher/RayModpackIndex.h"

class QLabel;
class QPushButton;

/**
 * @brief A square "app-store" tile in the Modpacks catalogue grid.
 *
 * Fixed 200×260 size so the parent FlowLayout can wrap tiles into rows when the window is wide
 * enough. Layout is vertical: icon 128×128 on top, bold name, a 2-line wrapped description, and
 * the primary action button at the bottom.
 *
 * Click handling is deliberately conservative — **only** the explicit action button fires an
 * action. Clicking on the icon / name / empty space of the tile does nothing (no full-card click
 * like Commit 2 used to do). Right-click anywhere on the tile emits contextMenuRequested; the
 * page draws a QMenu with state-dependent items (Jouer / Ouvrir le dossier / Supprimer …).
 */
class RayModpackCard : public QFrame {
    Q_OBJECT
   public:
    enum class State {
        Available,       ///< no matching installed instance → dimmed icon, button "Installer"
        Installed,       ///< instance exists and is up to date (v1: name-matched)           → button "Jouer"
        UpdateAvailable  ///< reserved for Commit 5 — button would read "Mettre à jour"
    };

    RayModpackCard(const RayModpack& pack, State state, const QString& instanceId, QWidget* parent = nullptr);

    const RayModpack& pack() const { return m_pack; }
    State state() const { return m_state; }
    const QString& instanceId() const { return m_instanceId; }

    /// Replace the icon (used when the async icon fetch resolves).
    void setIcon(const QPixmap& pixmap);

   signals:
    /// Fired when the user wants to install the pack (state == Available).
    void installClicked(const RayModpack& pack);
    /// Fired when the user wants to launch the matching instance (state == Installed).
    void playClicked(const QString& instanceId);
    /// Fired when the user wants to update the matching instance (state == UpdateAvailable, future).
    void updateClicked(const RayModpack& pack, const QString& instanceId);
    /// Fired when the tile receives a right-click. globalPos is pre-computed from QContextMenuEvent.
    void contextMenuRequested(const RayModpack& pack, const QString& instanceId, const QPoint& globalPos);

   protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

   private slots:
    void onActionButtonClicked();

   private:
    void applyState();

    RayModpack m_pack;
    State m_state;
    QString m_instanceId;

    QLabel* m_iconLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QPushButton* m_actionButton = nullptr;
};

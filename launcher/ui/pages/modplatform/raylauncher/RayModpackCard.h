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
#include <QString>

#include "modplatform/raylauncher/RayModpackIndex.h"

class QLabel;
class QPushButton;

/**
 * @brief A single modpack card in the catalogue grid.
 *
 * The card adapts its visual state (dimmed/full color, Install/Play action button) based on whether
 * the user already has a matching instance installed. Clicking anywhere on the card, or on the
 * explicit action button, emits the state-appropriate signal. Hover subtly highlights the card to
 * hint that it's actionable.
 *
 * This widget is deliberately dumb: state + matching is decided by RayModpackPage and passed in;
 * the card just renders and re-emits user intent.
 */
class RayModpackCard : public QFrame {
    Q_OBJECT
   public:
    enum class State {
        Available,       ///< Modpack is in the catalog but not installed on this machine
        Installed,       ///< Modpack is installed and up to date (or we can't tell — v1 heuristic)
        UpdateAvailable  ///< Reserved for Commit 3; unused for now
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
    /// Fired when the user wants to update the matching instance (state == UpdateAvailable).
    void updateClicked(const RayModpack& pack, const QString& instanceId);

   protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

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

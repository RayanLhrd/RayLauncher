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

#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QWidget>

#include "modplatform/raylauncher/RayModpackIndex.h"
#include "ui/pages/modplatform/raylauncher/RayModpackCard.h"

class QLabel;
class QPushButton;
class QScrollArea;
class FlowLayout;

/**
 * @brief The primary central widget: a grid of square modpack tiles.
 *
 * Lays catalogue tiles out with a FlowLayout (tiles wrap onto new rows as the window widens/narrows),
 * and offers a per-tile right-click context menu for Play / Open folder / Delete.
 * Actual work (InstanceImportTask, Application::launch, folder-open, delete confirm) is performed
 * by MainWindow — this widget just tracks state and emits signals.
 */
class RayModpackPage : public QWidget {
    Q_OBJECT
   public:
    explicit RayModpackPage(QWidget* parent = nullptr);
    ~RayModpackPage() override;

   signals:
    void installRequested(const RayModpack& pack);
    void playRequested(const QString& instanceId);
    void killRequested(const QString& instanceId);
    void updateRequested(const RayModpack& pack, const QString& instanceId);
    /// Open the instance directory in the system file explorer.
    void openFolderRequested(const QString& instanceId);
    /// Delete the instance (after confirmation — caller owns the confirm dialog + the tag check).
    void deleteRequested(const QString& instanceId);
    /// Open the "Mémoire allouée" picker for an installed instance. The pack carries the
    /// author's recommendation; the caller resolves the current value from instance.cfg.
    void memoryRequested(const RayModpack& pack, const QString& instanceId);
    /// Toggle a specific mod on/off for an installed instance. Implemented by MainWindow by
    /// locating @p jarPattern under `<instance>/.minecraft/mods/` (matches both `*.jar` and
    /// `*.jar.disabled`) and renaming. No-op silently if the file doesn't exist.
    void toggleModRequested(const QString& instanceId, const QString& jarPattern, const QString& label);

   private slots:
    void onIndexLoaded();
    void onIndexFailed(QString error);
    void onRefreshClicked();
    void onInstanceListChanged();
    void onCardContextMenu(const RayModpack& pack, const QString& instanceId, const QPoint& globalPos);

   private:
    void rebuildTiles();
    void addTile(const RayModpack& pack, RayModpackCard::State state, const QString& instanceId);
    void setStatus(const QString& text, bool isError);
    QString installedInstanceIdFor(const RayModpack& pack) const;
    void fetchIcon(RayModpackCard* card, const QUrl& url);

    /// Pings the GitHub Releases API for our own repo, parses the latest tag's version, and
    /// shows the update banner if it's higher than the running BuildConfig version. Silent
    /// no-op when updaterEnabled() is false (no PrismUpdater binary shipped in this build),
    /// when offline, or when the API call fails — we never block the UI for an update check.
    void checkForLauncherUpdate();
    void showLauncherUpdateBanner(const QString& latestVersion);

    /// Modal popup that pops in front of the user when a new version is detected, so they
    /// can't miss it. Only fires when the remote version differs from the value persisted
    /// in `RayLauncher_AcknowledgedUpdateVersion` — once the user has clicked "Mettre à jour"
    /// on this version (even if the install didn't end up replacing the binary), we won't
    /// pop up the modal again until a newer remote version ships. The banner above the tile
    /// grid still appears in that "already-acknowledged" state as a silent reminder.
    void promptLauncherUpdate(const QString& latestVersion);

    /// Triggers the actual update via the PrismUpdater binary and records the user's
    /// acknowledgment of @p version so neither the banner nor the modal ever resurface
    /// for it. Used by both the banner button and the modal's primary action.
    void acknowledgeAndUpdate(const QString& version);

    RayModpackIndexFetcher* m_fetcher = nullptr;

    QLabel* m_statusLabel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_tilesContainer = nullptr;
    FlowLayout* m_tilesLayout = nullptr;

    // Banner shown above the tile grid when a newer launcher release exists on GitHub. Hidden
    // until the version check finishes and reports an upgrade is available — see
    // checkForLauncherUpdate(). Click the inner button to spawn the PrismUpdater binary via
    // APPLICATION->triggerUpdateCheck().
    QWidget* m_updateBanner = nullptr;
    QLabel* m_updateBannerLabel = nullptr;

    // Kept so we can re-attach icons after a rebuild without redownloading — keyed by pack id.
    QHash<QString, QPixmap> m_iconCache;
};

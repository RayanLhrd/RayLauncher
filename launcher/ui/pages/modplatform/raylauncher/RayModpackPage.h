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

    RayModpackIndexFetcher* m_fetcher = nullptr;

    QLabel* m_statusLabel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_tilesContainer = nullptr;
    FlowLayout* m_tilesLayout = nullptr;

    // Kept so we can re-attach icons after a rebuild without redownloading — keyed by pack id.
    QHash<QString, QPixmap> m_iconCache;
};

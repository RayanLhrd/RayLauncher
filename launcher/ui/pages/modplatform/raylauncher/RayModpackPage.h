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
#include <QWidget>

#include "modplatform/raylauncher/RayModpackIndex.h"
#include "ui/pages/modplatform/raylauncher/RayModpackCard.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/**
 * @brief The primary "Modpacks" central widget shown in MainWindow.
 *
 * Fetches the RayLauncher catalogue (BuildConfig.RAYLAUNCHER_MODPACK_INDEX_URL) on construction
 * and renders one RayModpackCard per pack. Each card's state (Available / Installed) is resolved
 * by matching the catalogue entry's `name` against the user's installed instances — if a matching
 * instance exists, the card switches to Installed and its action button becomes "Jouer". Name-
 * based matching is a deliberate v1 simplification; proper tagging via an `RayLauncher_ModpackId`
 * setting on the instance is scheduled for the next commit together with the Update flow.
 *
 * Actions are emitted as signals so MainWindow wires them into its existing task machinery
 * without this widget needing to know about InstanceImportTask / Application::launch.
 */
class RayModpackPage : public QWidget {
    Q_OBJECT
   public:
    explicit RayModpackPage(QWidget* parent = nullptr);
    ~RayModpackPage() override;

   signals:
    void installRequested(const RayModpack& pack);
    void playRequested(const QString& instanceId);
    void updateRequested(const RayModpack& pack, const QString& instanceId);

   private slots:
    void onIndexLoaded();
    void onIndexFailed(QString error);
    void onRefreshClicked();
    void onInstanceListChanged();

   private:
    void rebuildCards();
    void setStatus(const QString& text, bool isError);
    QString installedInstanceIdFor(const RayModpack& pack) const;
    void fetchIcon(RayModpackCard* card, const QUrl& url);

    RayModpackIndexFetcher* m_fetcher = nullptr;

    QLabel* m_statusLabel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_cardsContainer = nullptr;
    QVBoxLayout* m_cardsLayout = nullptr;

    // Kept so we can re-attach icons after a rebuild without redownloading — keyed by pack id.
    QHash<QString, QPixmap> m_iconCache;
};

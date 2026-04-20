// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QWidget>
#include <optional>

#include "modplatform/raylauncher/RayModpackIndex.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

/**
 * @brief The primary "Modpacks" tab shown in MainWindow on startup.
 *
 * Fetches the RayLauncher modpack catalogue (BuildConfig.RAYLAUNCHER_MODPACK_INDEX_URL),
 * renders each pack as an item in a QListWidget (icon + name), and emits @ref installRequested
 * when the user triggers an install. The actual InstanceImportTask plumbing lives in MainWindow
 * so this widget stays free of app-wide side effects.
 *
 * For Commit 1 this is deliberately minimal — richer card rendering (state badges, descriptions,
 * Play/Update states) ships in subsequent commits.
 */
class RayModpackPage : public QWidget {
    Q_OBJECT
   public:
    explicit RayModpackPage(QWidget* parent = nullptr);
    ~RayModpackPage() override;

   signals:
    /// Emitted when the user confirms they want to install a specific modpack.
    void installRequested(const RayModpack& pack);

   private slots:
    void onIndexLoaded();
    void onIndexFailed(QString error);
    void onSelectionChanged();
    void onRefreshClicked();
    void onInstallClicked();
    void onItemDoubleClicked(QListWidgetItem* item);

   private:
    void setStatus(const QString& statusText, bool isError);
    void rebuildList();
    void fetchIconFor(QListWidgetItem* item, const QUrl& url);
    std::optional<RayModpack> currentModpack() const;

    RayModpackIndexFetcher* m_fetcher = nullptr;

    QLabel* m_statusLabel = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QPushButton* m_installButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
};

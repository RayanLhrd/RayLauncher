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

#include <QDialog>
#include <QHash>
#include <optional>

#include "modplatform/raylauncher/RayModpackIndex.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class RayModpackDialog : public QDialog {
    Q_OBJECT
   public:
    explicit RayModpackDialog(QWidget* parent = nullptr);
    ~RayModpackDialog() override;

    // Valid only after the dialog is accepted.
    std::optional<RayModpack> selectedModpack() const { return m_selected; }

   private slots:
    void onIndexLoaded();
    void onIndexFailed(QString error);
    void onSelectionChanged();
    void onRefreshClicked();
    void onItemDoubleClicked(QListWidgetItem* item);

   private:
    void accept() override;
    void setStatus(const QString& statusText, bool isError);
    void rebuildList();
    void fetchIconFor(QListWidgetItem* item, const QUrl& url);

    RayModpackIndexFetcher* m_fetcher = nullptr;
    std::optional<RayModpack> m_selected;

    // Widgets (all parented to this QDialog)
    QLabel* m_statusLabel = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QPushButton* m_installButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
};

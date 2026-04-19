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

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "net/NetJob.h"

struct RayModpack {
    QString id;
    QString name;
    QString description;
    QUrl iconUrl;
    QUrl mrpackUrl;
    QString version;
};

class RayModpackIndexFetcher : public QObject {
    Q_OBJECT
   public:
    RayModpackIndexFetcher(shared_qobject_ptr<QNetworkAccessManager> network, QString indexUrl, QObject* parent = nullptr);

    void fetch();
    bool isLoading() const { return m_job.get() != nullptr; }
    const QList<RayModpack>& modpacks() const { return m_modpacks; }
    QString lastError() const { return m_lastError; }

   signals:
    void loaded();
    void failed(QString error);

   private slots:
    void onDownloadFinished();
    void onDownloadFailed(QString reason);

   private:
    void succeed();
    void fail(const QString& error);

    shared_qobject_ptr<QNetworkAccessManager> m_network;
    QString m_indexUrl;
    NetJob::Ptr m_job;
    std::shared_ptr<QByteArray> m_data = std::make_shared<QByteArray>();
    QList<RayModpack> m_modpacks;
    QString m_lastError;
};

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

#include "RayModpackIndex.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Json.h"
#include "net/Download.h"

RayModpackIndexFetcher::RayModpackIndexFetcher(shared_qobject_ptr<QNetworkAccessManager> network, QString indexUrl, QObject* parent)
    : QObject(parent), m_network(std::move(network)), m_indexUrl(std::move(indexUrl))
{}

void RayModpackIndexFetcher::fetch()
{
    if (isLoading()) {
        qDebug() << "RayModpackIndexFetcher: fetch already in progress, ignoring";
        return;
    }
    if (m_indexUrl.isEmpty()) {
        fail(tr("No modpack index URL is configured."));
        return;
    }

    m_data->clear();
    m_modpacks.clear();

    NetJob::Ptr job{ new NetJob("RayLauncher modpack index", m_network) };
    job->addNetAction(Net::Download::makeByteArray(QUrl(m_indexUrl), m_data));
    job->setAskRetry(false);
    connect(job.get(), &NetJob::succeeded, this, &RayModpackIndexFetcher::onDownloadFinished);
    connect(job.get(), &NetJob::failed, this, &RayModpackIndexFetcher::onDownloadFailed);
    m_job.reset(job);
    job->start();
}

void RayModpackIndexFetcher::onDownloadFinished()
{
    m_job.reset();

    QList<RayModpack> parsed;
    try {
        QJsonDocument doc = Json::requireDocument(*m_data, "RayLauncher modpack index");
        QJsonObject root = Json::requireObject(doc, "RayLauncher modpack index");

        QJsonArray arr = Json::requireIsType<QJsonArray>(root, "modpacks", "modpacks");
        parsed.reserve(arr.size());
        for (const QJsonValue& entry : arr) {
            QJsonObject obj = Json::requireIsType<QJsonObject>(entry, "modpack entry");

            RayModpack pack;
            pack.id = Json::requireIsType<QString>(obj, "id", "modpack id");
            pack.name = Json::requireIsType<QString>(obj, "name", "modpack name");
            pack.mrpackUrl = QUrl(Json::requireIsType<QString>(obj, "mrpack_url", "mrpack_url"));

            if (obj.contains("description"))
                pack.description = Json::requireIsType<QString>(obj, "description", "description");
            if (obj.contains("icon_url"))
                pack.iconUrl = QUrl(Json::requireIsType<QString>(obj, "icon_url", "icon_url"));
            if (obj.contains("version"))
                pack.version = Json::requireIsType<QString>(obj, "version", "version");
            if (obj.contains("recommended_memory_mb"))
                pack.recommendedMemoryMb = Json::requireIsType<int>(obj, "recommended_memory_mb", "recommended_memory_mb");

            if (!pack.mrpackUrl.isValid()) {
                qWarning() << "Skipping modpack" << pack.id << "- invalid mrpack_url";
                continue;
            }
            parsed.append(pack);
        }
    } catch (const Json::JsonException& e) {
        fail(tr("Could not parse modpack index: %1").arg(e.cause()));
        m_data->clear();
        return;
    }

    m_data->clear();
    m_modpacks = std::move(parsed);
    succeed();
}

void RayModpackIndexFetcher::onDownloadFailed(QString reason)
{
    fail(tr("Could not fetch modpack index: %1").arg(reason));
}

void RayModpackIndexFetcher::succeed()
{
    m_lastError.clear();
    emit loaded();
}

void RayModpackIndexFetcher::fail(const QString& error)
{
    qDebug() << "RayModpackIndexFetcher failure:" << error;
    m_lastError = error;
    m_job.reset();
    emit failed(error);
}

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Freesm Launcher - Minecraft Launcher
 *  Copyright (C) 2025 so5iso4ka <so5iso4ka@icloud.com>
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

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "BaseInstance.h"
#include "RunningInstance.h"

#include "Activity.h"

#include "BuildConfig.h"

namespace {
enum class DiscordActivityType { Playing = 0, Streaming = 1, Listening = 2, Watching = 3, Custom = 4, Competing = 5 };

QJsonValue makeNonce()
{
    return QUuid::createUuid().toString();
}

QJsonObject makeActivityTimestamps(const qint64 startTime)
{
    QJsonObject timestamps;

    timestamps["start"] = startTime;

    return timestamps;
}

QJsonObject makeActivity(qint64 startTime, const QString& details)
{
    QJsonObject activity;

    activity["name"] = BuildConfig.LAUNCHER_DISPLAYNAME;
    activity["type"] = static_cast<int>(DiscordActivityType::Playing);
    if (!details.isEmpty())
        activity["details"] = details;
    activity["timestamps"] = makeActivityTimestamps(startTime);

    // Tell Discord which asset (uploaded on the developer-portal under the configured Application
    // ID) to render as the big square image on the user's profile. The key matches an asset name
    // set in the Rich Presence → Art Assets page; if Discord can't resolve it, no image renders
    // (no fallback, no error). large_text is the tooltip on hover.
    QJsonObject assets;
    assets["large_image"] = QStringLiteral("launcher_logo");
    assets["large_text"] = BuildConfig.LAUNCHER_DISPLAYNAME;
    activity["assets"] = assets;

    return activity;
}

QByteArray makeRequest(qint64 startTime, const QString& details)
{
    QJsonObject request;

    QJsonObject args;
    args["pid"] = APPLICATION->applicationPid();
    args["activity"] = makeActivity(startTime, details);

    request["args"] = args;
    request["nonce"] = makeNonce();
    request["cmd"] = "SET_ACTIVITY";

    return QJsonDocument(request).toJson();
}
}  // namespace

QByteArray generateActivity(const RunningInstance& instance)
{
    return makeRequest(instance.startedAt.toSecsSinceEpoch(), "Minecraft " + instance.ptr()->getVersionString());
}

QByteArray generateBackgroundActivity(qint64 startTime)
{
    return makeRequest(startTime, "");
}

QByteArray generateEmptyActivity()
{
    QJsonObject request;

    QJsonObject args;
    args["pid"] = APPLICATION->applicationPid();
    args["activity"] = QJsonValue::Null;

    request["args"] = args;
    request["nonce"] = makeNonce();
    request["cmd"] = "SET_ACTIVITY";

    return QJsonDocument(request).toJson();
}
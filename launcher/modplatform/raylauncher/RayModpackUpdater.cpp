// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayModpackUpdater.h"

#include <QFile>
#include <QSet>
#include <QTextStream>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"

RayModpackUpdater::RayModpackUpdater(const RayModpack& pack, InstancePtr instance, QWidget* parent)
    : m_pack(pack), m_instance(std::move(instance)), m_parentWidget(parent)
{
    setObjectName("RayModpackUpdater");
}

RayModpackUpdater::~RayModpackUpdater() = default;

bool RayModpackUpdater::isPreservedKey(const QString& key)
{
    // Personal/ergonomic settings — user-specific, carried forward across updates.
    static const QSet<QString> kExact = {
        QStringLiteral("fov"),
        QStringLiteral("mouseSensitivity"),
        QStringLiteral("renderDistance"),
        QStringLiteral("entityDistanceScaling"),
        QStringLiteral("biomeBlendRadius"),
        QStringLiteral("guiScale"),
        QStringLiteral("fullscreen"),
        QStringLiteral("fullscreenResolution"),
        QStringLiteral("enableVsync"),
        QStringLiteral("maxFps"),
        QStringLiteral("graphicsMode"),
        QStringLiteral("fancyGraphics"),
        QStringLiteral("ao"),
        QStringLiteral("clouds"),
        QStringLiteral("gamma"),
        QStringLiteral("lang"),
        QStringLiteral("chatOpacity"),
        QStringLiteral("chatHeightFocused"),
        QStringLiteral("chatHeightUnfocused"),
        QStringLiteral("chatScale"),
        QStringLiteral("chatWidth"),
        QStringLiteral("particles"),
        QStringLiteral("narrator"),
        QStringLiteral("autoJump"),
        QStringLiteral("toggleCrouch"),
        QStringLiteral("toggleSprint"),
        QStringLiteral("mainHand"),
        QStringLiteral("invertYMouse"),
        QStringLiteral("attackIndicator"),
        QStringLiteral("forceUnicodeFont"),
    };
    if (kExact.contains(key))
        return true;
    // All keybinds: key_key.attack, key_key.jump, key_key.use, etc.
    if (key.startsWith(QStringLiteral("key_")))
        return true;
    // Per-category audio volume: soundCategory_master, soundCategory_music, soundCategory_voice, ...
    if (key.startsWith(QStringLiteral("soundCategory_")))
        return true;
    return false;
}

QStringList RayModpackUpdater::extractPreservedLines(const QString& optionsTxtPath)
{
    QStringList out;
    QFile f(optionsTxtPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;  // No options.txt yet — nothing to preserve. Fresh Minecraft run will generate one.
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int sep = line.indexOf(QLatin1Char(':'));
        if (sep <= 0)
            continue;
        const QString key = line.left(sep).trimmed();
        if (isPreservedKey(key)) {
            out << line;
        }
    }
    return out;
}

void RayModpackUpdater::writePreservedLines(const QString& optionsTxtPath, const QStringList& preservedLines)
{
    if (preservedLines.isEmpty())
        return;

    // Build a lookup of preserved lines by key for quick replace-or-insert.
    QHash<QString, QString> preservedByKey;
    for (const QString& line : preservedLines) {
        const int sep = line.indexOf(QLatin1Char(':'));
        if (sep <= 0)
            continue;
        preservedByKey.insert(line.left(sep).trimmed(), line);
    }

    QStringList finalLines;

    // If the pack ships its own options.txt, walk it and swap matching keys in-place. Otherwise
    // we'll just write out the preserved keys as a fresh file — Minecraft fills in the defaults
    // for everything else on first launch.
    QFile in(optionsTxtPath);
    if (in.exists() && in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&in);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            const int sep = line.indexOf(QLatin1Char(':'));
            if (sep > 0) {
                const QString key = line.left(sep).trimmed();
                auto it = preservedByKey.find(key);
                if (it != preservedByKey.end()) {
                    finalLines << it.value();
                    preservedByKey.erase(it);
                    continue;
                }
            }
            finalLines << line;
        }
        in.close();
    }

    // Append any preserved keys the pack's options.txt didn't contain — so newly-added user
    // preferences aren't silently dropped.
    for (auto it = preservedByKey.constBegin(); it != preservedByKey.constEnd(); ++it) {
        finalLines << it.value();
    }

    QFile out(optionsTxtPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QTextStream wr(&out);
    for (const QString& line : finalLines) {
        wr << line << "\n";
    }
}

void RayModpackUpdater::executeTask()
{
    if (!m_instance) {
        emitFailed(tr("Instance introuvable."));
        return;
    }

    if (m_instance->isRunning()) {
        emitFailed(tr("Arrête Minecraft avant de mettre à jour ce modpack."));
        return;
    }

    auto mcInst = std::dynamic_pointer_cast<MinecraftInstance>(m_instance);
    if (!mcInst) {
        emitFailed(tr("Cette instance n'est pas un instance Minecraft valide."));
        return;
    }

    setStatus(tr("Sauvegarde de tes paramètres perso (touches, FOV, volumes…)…"));

    const QString optionsTxtPath = FS::PathCombine(mcInst->gameRoot(), QStringLiteral("options.txt"));
    m_preservedLines = extractPreservedLines(optionsTxtPath);

    // Capture name + group before we nuke the instance — InstanceImportTask needs them.
    m_instanceName = m_instance->name();
    m_instanceGroup = APPLICATION->instances()->getInstanceGroup(m_instance->id());
    const QString oldId = m_instance->id();

    setStatus(tr("Suppression de l'ancienne version…"));
    APPLICATION->instances()->deleteInstance(oldId);
    m_instance.reset();  // drop our shared_ptr so the instance can actually deallocate.

    setStatus(tr("Téléchargement et installation de la nouvelle version…"));
    auto* importTask = new InstanceImportTask(m_pack.mrpackUrl, m_parentWidget);
    importTask->setName(m_instanceName);
    if (!m_instanceGroup.isEmpty())
        importTask->setGroup(m_instanceGroup);

    m_wrappedImport = APPLICATION->instances()->wrapInstanceTask(importTask);
    connect(m_wrappedImport, &Task::succeeded, this, &RayModpackUpdater::onImportSucceeded);
    connect(m_wrappedImport, &Task::failed, this, &RayModpackUpdater::onImportFailed);
    // Forward progress and status from the import so the progress dialog shows what's happening.
    connect(m_wrappedImport, &Task::progress, this, [this](qint64 current, qint64 total) { setProgress(current, total); });
    connect(m_wrappedImport, &Task::status, this, [this](const QString& s) { setStatus(s); });
    // Self-cleanup so the wrapper doesn't leak past our own lifetime.
    connect(m_wrappedImport, &Task::succeeded, m_wrappedImport, &QObject::deleteLater);
    connect(m_wrappedImport, &Task::failed, m_wrappedImport, &QObject::deleteLater);
    connect(m_wrappedImport, &Task::aborted, m_wrappedImport, &QObject::deleteLater);
    m_wrappedImport->start();
}

void RayModpackUpdater::onImportSucceeded()
{
    setStatus(tr("Restauration de tes paramètres perso…"));

    // Find the freshly-imported instance by its name — it should be the last one matching.
    InstancePtr fresh;
    auto list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        InstancePtr inst = list->at(i);
        if (inst && inst->name().trimmed() == m_instanceName.trimmed()) {
            fresh = inst;  // keep going in case there are multiple — we want the newest
        }
    }
    if (!fresh) {
        emitFailed(tr("L'instance mise à jour est introuvable après l'installation."));
        return;
    }

    // Re-tag with the new version so future update checks work.
    fresh->settings()->set(QStringLiteral("RayLauncher_ModpackId"), m_pack.id);
    fresh->settings()->set(QStringLiteral("RayLauncher_ModpackVersion"), m_pack.version);

    // Restore preserved options into the freshly-written options.txt.
    auto mcFresh = std::dynamic_pointer_cast<MinecraftInstance>(fresh);
    if (mcFresh) {
        const QString optionsTxtPath = FS::PathCombine(mcFresh->gameRoot(), QStringLiteral("options.txt"));
        FS::ensureFilePathExists(optionsTxtPath);
        writePreservedLines(optionsTxtPath, m_preservedLines);
    }

    emitSucceeded();
}

void RayModpackUpdater::onImportFailed(QString reason)
{
    emitFailed(tr("Mise à jour échouée : %1").arg(reason));
}

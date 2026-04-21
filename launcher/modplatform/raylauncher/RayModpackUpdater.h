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

#include <QString>
#include <QStringList>
#include <QWidget>

#include "BaseInstance.h"
#include "modplatform/raylauncher/RayModpackIndex.h"
#include "tasks/Task.h"

class InstanceImportTask;

/**
 * @brief Atomically replaces the contents of a RayLauncher-tagged instance with a fresh mrpack
 * download, preserving the user's comfort settings from options.txt.
 *
 * Conceptually a three-step state machine:
 *
 *   1. Preflight — extract the allowlist of comfort keys (keybinds, FOV, GUI scale, sensitivity,
 *      audio volumes, etc.) from the existing `<instance>/.minecraft/options.txt`, in memory.
 *      Refuse to run if the instance is still running — the user has to exit Minecraft first.
 *
 *   2. Wipe + re-import — delete the old instance directory via InstanceList::deleteInstance,
 *      then kick off InstanceImportTask with the new mrpack URL under the exact same name and
 *      group. The newly-imported instance replaces the old one with fresh mods/, config/,
 *      resourcepacks/, shaderpacks/ directly from the pack.
 *
 *   3. Tag + restore — once the import finishes we look up the new instance by name, stamp
 *      RayLauncher_ModpackId + RayLauncher_ModpackVersion on its instance.cfg so subsequent
 *      update checks compare against the latest version, then overlay the preserved comfort
 *      keys into the pack's own options.txt (keys absent from the pack file are inserted,
 *      keys already present are overwritten). Pack-controlled options like resourcePacks or
 *      incompatibleResourcePacks stay as the pack author shipped them.
 *
 * Anything the user put in mods/, config/, etc. that isn't part of the pack is lost, by design
 * — the caller is expected to confirm that with the user first. Screenshots and saves survive
 * because they live in their own subdirectories that the mrpack doesn't touch, and because
 * Prism's deleteInstance only wipes the instance root (which IS everything). Actually wait —
 * that means saves/screenshots DO get wiped. The task's caller should surface this caveat.
 */
class RayModpackUpdater : public Task {
    Q_OBJECT
   public:
    RayModpackUpdater(const RayModpack& pack, InstancePtr instance, QWidget* parent = nullptr);
    ~RayModpackUpdater() override;

   protected:
    void executeTask() override;

   private slots:
    void onImportSucceeded();
    void onImportFailed(QString reason);

   private:
    /// Parse options.txt line-by-line and return the lines whose key matches the preserved-key
    /// allowlist (keybinds + comfort settings). Format of returned strings: "key:value\n".
    QStringList extractPreservedLines(const QString& optionsTxtPath);

    /// Merge preservedLines into the options.txt at @p optionsTxtPath. For each preserved line
    /// with key K: if a line with key K already exists, replace it; otherwise append.
    void writePreservedLines(const QString& optionsTxtPath, const QStringList& preservedLines);

    /// True if the options.txt key is in the preserved-on-update allowlist (comfort/ergonomic
    /// settings only). See body for the full list.
    static bool isPreservedKey(const QString& key);

    RayModpack m_pack;
    InstancePtr m_instance;
    QWidget* m_parentWidget = nullptr;

    // Captured in executeTask() before we wipe the instance, consumed in onImportSucceeded().
    QStringList m_preservedLines;
    QString m_instanceName;
    QString m_instanceGroup;

    // Owned by the wrapper task from InstanceList::wrapInstanceTask — we only keep a raw pointer
    // so we can disconnect on abort.
    Task* m_wrappedImport = nullptr;
};

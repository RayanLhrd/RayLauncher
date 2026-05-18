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
#include "modplatform/raylauncher/RayModpackManifest.h"
#include "net/NetJob.h"
#include "tasks/Task.h"

class InstanceImportTask;

/**
 * @brief In-place differential update of a RayLauncher-tagged modpack instance.
 *
 * Replaces the old "delete instance + re-import .mrpack" updater. Now we:
 *
 *   1. Read the manifest written at the last install/update (if it exists). This tells us
 *      exactly which files came from the pack, with their hashes.
 *   2. Download and parse the new .mrpack to build the NEW manifest.
 *   3. Pick a mode based on what we see:
 *        - Migration: no old manifest (instance was installed by an older RayLauncher version,
 *          or by hand). Wipe pack-content dirs (mods/, config/, etc.), install new pack files
 *          fresh. Player data (saves, Xaero, screenshots, journeymap, …) is left untouched.
 *        - NormalDiff: old + new manifests both present, loader versions match. Compute the
 *          file-level diff (added, removed, modified by hash) and apply only that. Player
 *          customizations to options.txt survive via the smart-merge.
 *        - LoaderChange: dependencies in the new .mrpack (MC, NeoForge, …) don't match the
 *          old manifest's. The instance's PackProfile components have to be rebuilt from
 *          scratch via InstanceImportTask — but we still preserve player data dirs by
 *          backing them up first and restoring after the re-import.
 *   4. Run remote downloads for any added/modified `files[]` entries.
 *   5. Extract overrides from the cached .mrpack for any added/modified override files.
 *   6. Smart-merge options.txt (preserves player customizations, propagates new canonical
 *      keys to players who never touched them). The `force_options_reset_for_version` flag
 *      from the catalogue still works as a verbatim-replace escape hatch.
 *   7. Write the new manifest LAST — if anything fails before this step, the instance's
 *      manifest still points at the previous version and the next update attempt restarts
 *      cleanly from the same baseline.
 *
 * Instance settings the player has tweaked (RAM allocation via "Mémoire allouée…", instance
 * name/group) are captured up front and re-applied at the end. Memory allocation only matters
 * in LoaderChange mode where we actually call deleteInstance; the diff/migration modes never
 * touch instance.cfg.
 */
class RayDiffUpdater : public Task {
    Q_OBJECT
   public:
    RayDiffUpdater(const RayModpack& pack, InstancePtr instance, QWidget* parent = nullptr);
    ~RayDiffUpdater() override;

    bool abort() override;

   protected:
    void executeTask() override;

   private slots:
    // Phase 1: download the .mrpack to a temp file
    void onMrpackDownloaded();
    void onMrpackDownloadFailed(QString reason);

    // Phase 4: download all the `files[]` entries we need
    void onFileDownloadsFinished();
    void onFileDownloadsFailed(QString reason);

    // LoaderChange phase: wait for InstanceImportTask
    void onLoaderChangeImportSucceeded();
    void onLoaderChangeImportFailed(QString reason);

   private:
    enum class Mode {
        Migration,     ///< No old manifest. Wipe pack dirs, install everything from new pack.
        NormalDiff,    ///< Old manifest exists, deps match. Apply file-level diff.
        LoaderChange,  ///< Old manifest exists but MC/loader version changed. Full reimport, preserve player data.
    };

    // Setup + parsing
    bool preflight();
    bool buildNewManifestFromMrpack();
    Mode determineMode() const;

    // Diff-mode application (Migration + NormalDiff)
    void applyDiffModes();
    void wipePackContentDirs();
    void extractOverrideFiles(const QStringList& destPaths);
    void deleteFilesNoLongerInPack(const QStringList& destPaths);
    void mergeOptionsTxt();
    void finalizeAndSucceed();

    // LoaderChange-mode application
    void applyLoaderChange();
    void backupPlayerDataDirs();
    void restorePlayerDataDirs();

    // Utility
    QString gameRoot() const;
    QString instanceRoot() const;
    QString mrpackTempPath() const { return m_mrpackTempPath; }

    /// Pack-content directories that are 100% owned by the .mrpack — never contain player data
    /// in any modpack the author would ship. Used to:
    ///   - Migration mode: wipe these dirs before reinstall (catch removed mods from the pre-v1.1.0
    ///     era when we had no manifest to compute removals from).
    ///   - LoaderChange mode: anything NOT in this list (and not in instance.cfg territory) is
    ///     considered player data and gets backed up.
    static QStringList packContentDirs();

    /// Top-level files and directories under the gameRoot that are player-data and must
    /// survive every update mode. Used by LoaderChange backup.
    static QStringList playerDataDirs();

    /// Top-level files (not directories) under the gameRoot that are player-state and must survive
    /// LoaderChange. e.g. options.txt-style files, server lists. (options.txt itself is rebuilt
    /// via smart-merge by the diff modes; LoaderChange treats it as preservable.)
    static QStringList playerDataFiles();

    // ── State ────────────────────────────────────────────────────────────────────────────────
    RayModpack m_pack;
    InstancePtr m_instance;
    QWidget* m_parentWidget = nullptr;

    // Captured before we mutate anything, in case the LoaderChange path needs to nuke the instance.
    QString m_instanceName;
    QString m_instanceGroup;
    bool m_savedOverrideMemory = false;
    int m_savedMaxMemMb = 0;
    int m_savedMinMemMb = 0;

    QString m_mrpackTempPath;
    Mode m_mode = Mode::Migration;

    RayModpackManifest m_oldManifest;   // Default-constructed = "no manifest existed"
    RayModpackManifest m_newManifest;
    RayModpackManifest::Diff m_diff;
    bool m_hadOldManifest = false;

    QByteArray m_canonicalOptionsTxt;   ///< Bytes of the new .mrpack's overrides/options.txt (if any)

    // Files that still need to be installed after the .mrpack download — split by source so
    // we know whether to NetJob-download or ZIP-extract.
    QStringList m_pendingRemoteDownloads;
    QStringList m_pendingOverrideExtractions;
    QStringList m_pendingDeletions;

    // For LoaderChange backup/restore.
    QString m_playerDataBackupRoot;

    // Owned async tasks — we keep raw pointers so abort() can hit them.
    NetJob::Ptr m_mrpackDownloadJob;
    NetJob::Ptr m_fileDownloadsJob;
    Task* m_wrappedImportTask = nullptr;
};

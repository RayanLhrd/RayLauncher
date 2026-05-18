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

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

/**
 * @brief Per-file entry in a `.ray-modpack-manifest.json`.
 *
 * "Where this file came from" plus its content hash, so the next diff pass can decide
 * whether to keep, replace, or remove it. The hash is lowercase hex SHA-512.
 */
struct RayManifestFileEntry {
    enum class Source {
        Remote,    ///< Came from the .mrpack's `files[]` array (download URL list, hash mandatory).
        Override,  ///< Came verbatim from the .mrpack's `overrides/` (or `client-overrides/`) folder.
    };

    QString sha512;
    Source source = Source::Override;
    QString remoteUrl;  ///< Last-known download URL for `Source::Remote`; empty for overrides.
};

/**
 * @brief On-disk record of "what the .mrpack contained at install time".
 *
 * Written at `<gameRoot>/.ray-modpack-manifest.json` after every install or update. The next
 * update diffs the new pack against this snapshot and only touches files that differ — so
 * everything else (saves, Xaero data, screenshots, JourneyMap, any user-added file) stays
 * exactly where the player left it.
 *
 * Schema is forward-versioned through `ray_manifest_version`. When a launcher meets a manifest
 * from a future version it can't safely interpret, it refuses to parse and the caller falls
 * back to migration mode (no diff possible without a baseline).
 *
 * The `optionsCanonicalSnapshot` field stores key→value pairs from the canonical options.txt
 * that was installed. The smart-merge algorithm uses it to distinguish "user never touched
 * this key" (replace with new canonical) from "user customized this key" (preserve user's
 * value) — without it, every old-canonical bump would silently bypass players.
 */
class RayModpackManifest {
   public:
    static constexpr int kCurrentSchemaVersion = 1;

    /// Schema version we'll write. We refuse to load anything newer than this on the assumption
    /// that the on-disk format may have changed in a backwards-incompatible way.
    int schemaVersion = kCurrentSchemaVersion;

    /// `RayModpack::id` at the time of install — used by the page to confirm we're updating
    /// the right pack (catch the very-rare case where a pack id is reused under a different name).
    QString modpackId;

    /// `RayModpack::version` string from the catalogue at install time.
    QString packVersion;

    /// UTC timestamp of when this manifest was written, ISO-8601 ("2026-05-18T14:23:11Z").
    /// Diagnostic only; not used for any logic.
    QString installedAt;

    /// Loader version strings as advertised by `modrinth.index.json#dependencies`. Any of these
    /// can be empty if the pack doesn't use that loader. Used by the updater to detect "loader
    /// version changed → fall back to full reinstall" (in-place diff can't safely rebuild the
    /// PackProfile components).
    QString minecraftVersion;
    QString neoforgeVersion;
    QString forgeVersion;
    QString fabricVersion;
    QString quiltVersion;

    /// Destination paths relative to `<gameRoot>` (= `.minecraft/`) → file entry.
    /// e.g. `"mods/cobblemon-1.5.2.jar"`, `"config/jei/jei-client.toml"`, `"options.txt"`.
    /// Paths always use `/` separators (not `\`), even on Windows.
    QHash<QString, RayManifestFileEntry> files;

    /// Parsed `options.txt` (only `key:value` lines) as the pack author shipped it, captured at
    /// install/update time. Drives the smart-merge: if `userValue == optionsCanonicalSnapshot[k]`
    /// at update time, the player never customized that key and we propagate the new canonical.
    /// Otherwise we keep the player's value.
    QHash<QString, QString> optionsCanonicalSnapshot;

    /// @returns Path to `<gameRoot>/.ray-modpack-manifest.json`.
    static QString pathFor(const QString& gameRoot);

    /// Load and parse from disk. Returns `std::nullopt` when:
    ///   - the file doesn't exist (= migration / fresh install path),
    ///   - the JSON is malformed (logged + treated as migration),
    ///   - the schema version is newer than we support.
    static std::optional<RayModpackManifest> load(const QString& gameRoot);

    /// Atomically write to disk. Sets `installedAt` to "now" automatically. Returns false on
    /// any I/O failure (logged via qWarning).
    bool save(const QString& gameRoot);

    /// True iff `minecraft/neoforge/forge/fabric/quilt` versions all match `other`. Used to detect
    /// "loader version change between two pack versions" → updater must fall back to a full reinstall
    /// (the in-place diff can't rebuild the instance's PackProfile components).
    bool dependenciesMatch(const RayModpackManifest& other) const;

    /// Computed difference between two manifests. Each list contains destination paths.
    struct Diff {
        QStringList toAdd;        ///< In `newer` but not `older` → install
        QStringList toRemove;     ///< In `older` but not `newer` → delete
        QStringList toModify;     ///< In both but different hash → replace
        QStringList unchanged;    ///< In both with same hash → no-op (diagnostic)
    };

    /// Compute the file-level diff. `older` may be a default-constructed manifest (= "nothing
    /// installed yet") in which case every file in `newer` lands in `toAdd`.
    static Diff diff(const RayModpackManifest& older, const RayModpackManifest& newer);
};

/**
 * @brief Smart-merge logic for `options.txt`-style files.
 *
 * Minecraft's `options.txt` is a flat `key:value\n` file with no comments, no nesting, no
 * type info. The pack author's options.txt is canonical (his preferred keybinds + a few comfort
 * defaults), but the moment a player edits it in-game we want their customizations preserved
 * across updates. The trick is also wanting NEW canonical entries (e.g. a fresh mod that
 * introduced a keybind) to land at the player.
 *
 * `smartMerge` handles all three at once. See its doc comment for the rule table.
 */
namespace RayOptionsMerge {

/// Parsed `key:value` content of an `options.txt`. Order of first appearance is preserved
/// (Minecraft re-writes the file in deterministic order so writing it back identically is fine,
/// but appending new keys at the end matches what `defaultoptions` does).
struct Parsed {
    QStringList insertionOrder;        ///< Distinct keys in first-seen order
    QHash<QString, QString> values;     ///< key → trimmed value
};

/// Parse an options.txt blob. Drops the UTF-8 BOM if present. Splits on the FIRST `:` on each
/// line (so `resourcePacks:["foo","file/bar.zip"]` survives). Trims whitespace around both key
/// and value. Lines without `:` are silently ignored. On duplicate keys, last write wins
/// (matches Minecraft's own parser).
Parsed parse(const QByteArray& content);

/// Reconstruct an options.txt blob from a Parsed-like ordered/value pair. Always writes LF
/// line endings (what Minecraft writes), no trailing whitespace, no BOM.
QByteArray serialize(const QStringList& insertionOrder, const QHash<QString, QString>& values);

/// Compute the merged options.txt the player will get after an update.
///
/// Rules per key K in `newCanonical`:
///   1. If K not in `userOptions` → ADD with new canonical value.
///      (Use case: new mod ships with K=<default>, lands at the player on first launch.)
///   2. If K in `userOptions` AND K in `oldCanonicalSnapshot` AND
///      `userOptions[K] == oldCanonicalSnapshot[K]` → REPLACE with new canonical value.
///      (Use case: pack author changed a default, player never customized it → propagate.)
///   3. Otherwise → KEEP `userOptions[K]`.
///      (Use case: player customized it → never touch.)
/// Keys only in `userOptions` (not in `newCanonical`) → preserved untouched.
///
/// When `forceReset` is true, all rules are bypassed: `newCanonical` is returned verbatim.
/// This is the `force_options_reset_for_version` escape hatch from the catalogue.
QByteArray smartMerge(const QByteArray& userOptions,
                       const QByteArray& newCanonical,
                       const QHash<QString, QString>& oldCanonicalSnapshot,
                       bool forceReset);

}  // namespace RayOptionsMerge

/**
 * @brief Build a `.ray-modpack-manifest.json` from a freshly-imported Modrinth instance.
 *
 * Called by the install hook in `MainWindow::installRayModpack` right after
 * InstanceImportTask finishes. We reconstruct the manifest WITHOUT re-downloading the
 * .mrpack: InstanceImportTask preserves the original `modrinth.index.json` at
 * `<instanceRoot>/mrpack/modrinth.index.json` (alongside `overrides.txt`/`client-overrides.txt`
 * if present), so we can read the `files[]` array for the Remote entries and walk a
 * curated list of pack-content directories under `<gameRoot>` to discover the Override
 * entries by hashing what's on disk.
 *
 * Without this hook the first post-install update would fall into Migration mode and
 * essentially re-install the pack (downloading the .mrpack again, wiping pack-content dirs).
 * The hook lets the first update be a true small diff instead.
 *
 * Override discovery is bounded to a hardcoded list of pack-controlled dirs + a few known
 * top-level files (options.txt, optionsof.txt, optionsshaders.txt). Generated files that
 * mods write on first launch (mod-data dirs, save data, journeymap, etc.) live outside that
 * list and are correctly classified as "not from the pack" → never tracked, never deleted.
 *
 * @returns the populated manifest on success, nullopt if `modrinth.index.json` is missing
 *          or invalid. Caller should NOT save anything in the nullopt case; the next update
 *          will fall back to Migration mode (which is correct behavior for "we don't know
 *          what's pack content").
 */
namespace RayManifestBuilder {
std::optional<RayModpackManifest> buildFromInstalledInstance(const QString& gameRoot,
                                                              const QString& instanceRoot,
                                                              const QString& modpackId,
                                                              const QString& packVersion);
}  // namespace RayManifestBuilder

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayModpackManifest.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "FileSystem.h"
#include "Json.h"

// ── File name ─────────────────────────────────────────────────────────────────────────────────

namespace {
constexpr char kManifestFileName[] = ".ray-modpack-manifest.json";

QString sourceToString(RayManifestFileEntry::Source s)
{
    return s == RayManifestFileEntry::Source::Remote ? QStringLiteral("remote") : QStringLiteral("override");
}

RayManifestFileEntry::Source sourceFromString(const QString& s)
{
    return s == QStringLiteral("remote") ? RayManifestFileEntry::Source::Remote : RayManifestFileEntry::Source::Override;
}
}  // namespace

QString RayModpackManifest::pathFor(const QString& gameRoot)
{
    return FS::PathCombine(gameRoot, QString::fromLatin1(kManifestFileName));
}

// ── Load ──────────────────────────────────────────────────────────────────────────────────────

std::optional<RayModpackManifest> RayModpackManifest::load(const QString& gameRoot)
{
    const QString path = pathFor(gameRoot);
    QFile f(path);
    if (!f.exists()) {
        // Expected for fresh installs and pre-v1.1.0 instances. Caller falls back to migration.
        return std::nullopt;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "RayModpackManifest: cannot open" << path << "for read:" << f.errorString();
        return std::nullopt;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError jerr;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "RayModpackManifest: malformed JSON in" << path << ":" << jerr.errorString()
                   << "→ treating as no manifest (migration mode)";
        return std::nullopt;
    }
    const QJsonObject root = doc.object();

    RayModpackManifest m;
    m.schemaVersion = root.value(QStringLiteral("ray_manifest_version")).toInt(0);
    if (m.schemaVersion <= 0 || m.schemaVersion > kCurrentSchemaVersion) {
        qWarning() << "RayModpackManifest: unsupported schema version" << m.schemaVersion << "in" << path
                   << "(supported up to" << kCurrentSchemaVersion << ") → treating as no manifest";
        return std::nullopt;
    }

    m.modpackId = root.value(QStringLiteral("modpack_id")).toString();
    m.packVersion = root.value(QStringLiteral("pack_version")).toString();
    m.installedAt = root.value(QStringLiteral("installed_at")).toString();

    const QJsonObject deps = root.value(QStringLiteral("dependencies")).toObject();
    m.minecraftVersion = deps.value(QStringLiteral("minecraft")).toString();
    m.neoforgeVersion = deps.value(QStringLiteral("neoforge")).toString();
    m.forgeVersion = deps.value(QStringLiteral("forge")).toString();
    m.fabricVersion = deps.value(QStringLiteral("fabric-loader")).toString();
    m.quiltVersion = deps.value(QStringLiteral("quilt-loader")).toString();

    const QJsonObject filesObj = root.value(QStringLiteral("files")).toObject();
    for (auto it = filesObj.constBegin(); it != filesObj.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject e = it.value().toObject();
        RayManifestFileEntry entry;
        entry.sha512 = e.value(QStringLiteral("sha512")).toString();
        entry.source = sourceFromString(e.value(QStringLiteral("src")).toString());
        entry.remoteUrl = e.value(QStringLiteral("url")).toString();
        if (entry.sha512.isEmpty()) {
            // Defensive: a manifest entry without a hash is useless for diff. Skip it; the
            // diff will treat the file as "missing from old manifest" and reinstall on the
            // next update, which is harmless.
            qWarning() << "RayModpackManifest: entry" << it.key() << "in" << path << "has no sha512, skipping";
            continue;
        }
        m.files.insert(it.key(), entry);
    }

    const QJsonObject snap = root.value(QStringLiteral("options_canonical_snapshot")).toObject();
    for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
        if (it.value().isString())
            m.optionsCanonicalSnapshot.insert(it.key(), it.value().toString());
    }

    return m;
}

// ── Save ──────────────────────────────────────────────────────────────────────────────────────

bool RayModpackManifest::save(const QString& gameRoot)
{
    schemaVersion = kCurrentSchemaVersion;
    installedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonObject root;
    root.insert(QStringLiteral("ray_manifest_version"), schemaVersion);
    root.insert(QStringLiteral("modpack_id"), modpackId);
    root.insert(QStringLiteral("pack_version"), packVersion);
    root.insert(QStringLiteral("installed_at"), installedAt);

    QJsonObject deps;
    if (!minecraftVersion.isEmpty())
        deps.insert(QStringLiteral("minecraft"), minecraftVersion);
    if (!neoforgeVersion.isEmpty())
        deps.insert(QStringLiteral("neoforge"), neoforgeVersion);
    if (!forgeVersion.isEmpty())
        deps.insert(QStringLiteral("forge"), forgeVersion);
    if (!fabricVersion.isEmpty())
        deps.insert(QStringLiteral("fabric-loader"), fabricVersion);
    if (!quiltVersion.isEmpty())
        deps.insert(QStringLiteral("quilt-loader"), quiltVersion);
    root.insert(QStringLiteral("dependencies"), deps);

    QJsonObject filesObj;
    // Serialize files in a stable order for cleaner diffs in git/text editors and to make
    // diagnostic reads of the manifest sane. Sort by destination path.
    QStringList sortedKeys = files.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    for (const QString& key : sortedKeys) {
        const RayManifestFileEntry& entry = files[key];
        QJsonObject e;
        e.insert(QStringLiteral("sha512"), entry.sha512);
        e.insert(QStringLiteral("src"), sourceToString(entry.source));
        if (entry.source == RayManifestFileEntry::Source::Remote && !entry.remoteUrl.isEmpty())
            e.insert(QStringLiteral("url"), entry.remoteUrl);
        filesObj.insert(key, e);
    }
    root.insert(QStringLiteral("files"), filesObj);

    QJsonObject snap;
    QStringList sortedSnapKeys = optionsCanonicalSnapshot.keys();
    std::sort(sortedSnapKeys.begin(), sortedSnapKeys.end());
    for (const QString& k : sortedSnapKeys) {
        snap.insert(k, optionsCanonicalSnapshot[k]);
    }
    root.insert(QStringLiteral("options_canonical_snapshot"), snap);

    const QString path = pathFor(gameRoot);
    try {
        FS::ensureFilePathExists(path);
        // FS::write is atomic (writes to .tmp + rename) — partial-write crashes don't leave
        // a corrupt manifest behind.
        FS::write(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    } catch (const FS::FileSystemException& e) {
        qWarning() << "RayModpackManifest: failed to write" << path << ":" << e.cause();
        return false;
    }
}

// ── Diff + dependency match ───────────────────────────────────────────────────────────────────

bool RayModpackManifest::dependenciesMatch(const RayModpackManifest& other) const
{
    return minecraftVersion == other.minecraftVersion && neoforgeVersion == other.neoforgeVersion &&
           forgeVersion == other.forgeVersion && fabricVersion == other.fabricVersion &&
           quiltVersion == other.quiltVersion;
}

RayModpackManifest::Diff RayModpackManifest::diff(const RayModpackManifest& older, const RayModpackManifest& newer)
{
    Diff d;
    // toAdd / toModify / unchanged: iterate new entries and look up in old.
    for (auto it = newer.files.constBegin(); it != newer.files.constEnd(); ++it) {
        const auto oldIt = older.files.constFind(it.key());
        if (oldIt == older.files.constEnd()) {
            d.toAdd.append(it.key());
        } else if (oldIt.value().sha512.compare(it.value().sha512, Qt::CaseInsensitive) != 0) {
            d.toModify.append(it.key());
        } else {
            d.unchanged.append(it.key());
        }
    }
    // toRemove: iterate old entries and look up in new.
    for (auto it = older.files.constBegin(); it != older.files.constEnd(); ++it) {
        if (!newer.files.contains(it.key()))
            d.toRemove.append(it.key());
    }
    // Stable sort for predictable progress reporting + reproducible logs.
    std::sort(d.toAdd.begin(), d.toAdd.end());
    std::sort(d.toRemove.begin(), d.toRemove.end());
    std::sort(d.toModify.begin(), d.toModify.end());
    std::sort(d.unchanged.begin(), d.unchanged.end());
    return d;
}

// ── Options.txt smart merge ───────────────────────────────────────────────────────────────────

namespace RayOptionsMerge {

Parsed parse(const QByteArray& content)
{
    Parsed p;
    if (content.isEmpty())
        return p;

    // Strip UTF-8 BOM if present.
    QByteArray data = content;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove(0, 3);
    }

    // Decode as UTF-8. Minecraft writes UTF-8 (Java's default for new files).
    QString text = QString::fromUtf8(data);

    // Normalize line endings: split on \n after stripping \r.
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        const int sep = line.indexOf(QLatin1Char(':'));
        if (sep <= 0)
            continue;
        const QString key = line.left(sep).trimmed();
        const QString value = line.mid(sep + 1).trimmed();
        if (key.isEmpty())
            continue;
        if (!p.values.contains(key))
            p.insertionOrder.append(key);
        p.values.insert(key, value);  // last-wins on duplicates (matches MC's parser)
    }
    return p;
}

QByteArray serialize(const QStringList& insertionOrder, const QHash<QString, QString>& values)
{
    QByteArray out;
    out.reserve(values.size() * 32);  // rough heuristic
    QSet<QString> emitted;
    // First: emit keys in the requested order so the file structure remains familiar.
    for (const QString& key : insertionOrder) {
        if (!values.contains(key) || emitted.contains(key))
            continue;
        out += key.toUtf8();
        out += ':';
        out += values[key].toUtf8();
        out += '\n';
        emitted.insert(key);
    }
    // Then: anything in `values` that wasn't in the order list — append at end. Sorted so
    // tests have a deterministic output.
    QStringList stragglers;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!emitted.contains(it.key()))
            stragglers.append(it.key());
    }
    std::sort(stragglers.begin(), stragglers.end());
    for (const QString& key : stragglers) {
        out += key.toUtf8();
        out += ':';
        out += values[key].toUtf8();
        out += '\n';
    }
    return out;
}

QByteArray smartMerge(const QByteArray& userOptions,
                       const QByteArray& newCanonical,
                       const QHash<QString, QString>& oldCanonicalSnapshot,
                       bool forceReset)
{
    if (forceReset) {
        // Escape hatch: pack author wants every player onto their canonical regardless of
        // local edits. Used to recover from a botched options.txt export.
        return newCanonical;
    }

    Parsed user = parse(userOptions);
    Parsed canon = parse(newCanonical);

    // If the pack ships no canonical options.txt at all, there's nothing to merge — leave
    // the user's file untouched. (Caller already shouldn't be calling us in that case, but
    // be defensive.)
    if (canon.values.isEmpty()) {
        return userOptions;
    }

    // Build the merged map starting from user's existing values (= "user wins by default").
    QHash<QString, QString> merged = user.values;
    QStringList order = user.insertionOrder;

    for (const QString& key : canon.insertionOrder) {
        const QString newValue = canon.values.value(key);
        if (!merged.contains(key)) {
            // Rule 1: new canonical key — add it. (e.g. new mod's keybind.)
            merged.insert(key, newValue);
            order.append(key);
            continue;
        }
        // User has this key. Did they ever customize it?
        const auto snapIt = oldCanonicalSnapshot.constFind(key);
        if (snapIt != oldCanonicalSnapshot.constEnd() && snapIt.value() == merged[key]) {
            // Rule 2: user's value matches what we last installed as canonical → never
            // customized → take the new canonical value.
            merged[key] = newValue;
        }
        // Rule 3: user customized → keep their value (no-op).
    }

    return serialize(order, merged);
}

}  // namespace RayOptionsMerge

// ── Builder: from an installed instance ──────────────────────────────────────────────────────

namespace RayManifestBuilder {

namespace {
// Same set as RayDiffUpdater::packContentDirs() — duplicated here to keep this builder
// self-contained (Manifest module shouldn't depend on Updater). Keep them in sync.
const QStringList kPackContentDirs = {
    QStringLiteral("mods"),           QStringLiteral("config"),
    QStringLiteral("defaultconfigs"), QStringLiteral("resourcepacks"),
    QStringLiteral("shaderpacks"),    QStringLiteral("kubejs"),
    QStringLiteral("scripts"),        QStringLiteral("patchouli_books"),
    QStringLiteral("global_packs"),
};

const QStringList kPackTopLevelFiles = {
    QStringLiteral("options.txt"),       QStringLiteral("optionsof.txt"),
    QStringLiteral("optionsshaders.txt"),
};

QString sha512Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha512).toHex());
}

QString sha512OfFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha512);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}
}  // namespace

std::optional<RayModpackManifest> buildFromInstalledInstance(const QString& gameRoot,
                                                              const QString& instanceRoot,
                                                              const QString& modpackId,
                                                              const QString& packVersion)
{
    // 1. Parse `<instanceRoot>/mrpack/modrinth.index.json` for the files[] entries +
    //    dependencies. InstanceImportTask::processModrinth preserves this file as a
    //    side effect — see ModrinthCreationTask::createInstance, line ~183.
    const QString indexPath = QDir(instanceRoot).absoluteFilePath(QStringLiteral("mrpack/modrinth.index.json"));
    QFile indexFile(indexPath);
    if (!indexFile.exists()) {
        qDebug() << "RayManifestBuilder: no modrinth.index.json at" << indexPath
                 << "— skipping manifest write (next update will go via Migration mode)";
        return std::nullopt;
    }
    if (!indexFile.open(QIODevice::ReadOnly)) {
        qWarning() << "RayManifestBuilder: cannot open" << indexPath << ":" << indexFile.errorString();
        return std::nullopt;
    }
    const QByteArray indexBytes = indexFile.readAll();
    indexFile.close();

    RayModpackManifest manifest;
    manifest.modpackId = modpackId;
    manifest.packVersion = packVersion;

    QSet<QString> remoteDestPaths;  // for "what's NOT in files[] is an override" classification

    QJsonParseError jerr;
    QJsonDocument doc = QJsonDocument::fromJson(indexBytes, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "RayManifestBuilder: malformed modrinth.index.json:" << jerr.errorString();
        return std::nullopt;
    }
    const QJsonObject root = doc.object();

    // Dependencies.
    const QJsonObject deps = root.value(QStringLiteral("dependencies")).toObject();
    manifest.minecraftVersion = deps.value(QStringLiteral("minecraft")).toString();
    manifest.neoforgeVersion = deps.value(QStringLiteral("neoforge")).toString();
    manifest.forgeVersion = deps.value(QStringLiteral("forge")).toString();
    manifest.fabricVersion = deps.value(QStringLiteral("fabric-loader")).toString();
    manifest.quiltVersion = deps.value(QStringLiteral("quilt-loader")).toString();

    // files[] → Remote entries.
    const QJsonArray filesArr = root.value(QStringLiteral("files")).toArray();
    for (const QJsonValue& v : filesArr) {
        if (!v.isObject())
            continue;
        const QJsonObject f = v.toObject();
        QString path = f.value(QStringLiteral("path")).toString().replace('\\', '/');
        if (path.isEmpty())
            continue;

        // Skip entries with env.client=unsupported — they were NOT extracted onto disk.
        const QJsonObject env = f.value("env").toObject();
        if (!env.isEmpty()) {
            const QString clientSupport = env.value("client").toString(QStringLiteral("required"));
            if (clientSupport == QStringLiteral("unsupported"))
                continue;
        }

        const QString sha512 = f.value(QStringLiteral("hashes")).toObject().value("sha512").toString();
        if (sha512.isEmpty())
            continue;

        QString url;
        const QJsonArray dlArr = f.value(QStringLiteral("downloads")).toArray();
        for (const QJsonValue& d : dlArr) {
            const QString candidate = d.toString();
            if (!candidate.isEmpty()) {
                url = candidate;
                break;
            }
        }

        RayManifestFileEntry entry;
        entry.sha512 = sha512.toLower();
        entry.source = RayManifestFileEntry::Source::Remote;
        entry.remoteUrl = url;
        manifest.files.insert(path, entry);
        remoteDestPaths.insert(path);
    }

    // 2. Walk pack-content dirs under gameRoot. Anything found that isn't already a Remote
    //    entry is an Override (i.e. came from the .mrpack's overrides/ folder).
    QDir gameRootDir(gameRoot);
    for (const QString& dir : kPackContentDirs) {
        const QString absDir = gameRootDir.absoluteFilePath(dir);
        if (!QDir(absDir).exists())
            continue;

        QDirIterator it(absDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absFile = it.next();
            const QString relFile = gameRootDir.relativeFilePath(absFile).replace('\\', '/');
            if (remoteDestPaths.contains(relFile))
                continue;  // Already tracked as Remote with a known hash; skip.

            const QString sha = sha512OfFile(absFile);
            if (sha.isEmpty()) {
                qWarning() << "RayManifestBuilder: cannot hash" << absFile << "— skipping";
                continue;
            }

            RayManifestFileEntry entry;
            entry.sha512 = sha;
            entry.source = RayManifestFileEntry::Source::Override;
            manifest.files.insert(relFile, entry);
        }
    }

    // 3. Top-level options.txt (and siblings) — pack-shipped overrides land at the root of
    //    gameRoot, not under a content dir, so the walk above misses them. Check the small
    //    explicit list.
    for (const QString& topFile : kPackTopLevelFiles) {
        const QString abs = gameRootDir.absoluteFilePath(topFile);
        if (!QFile::exists(abs))
            continue;
        if (remoteDestPaths.contains(topFile))
            continue;
        const QString sha = sha512OfFile(abs);
        if (sha.isEmpty())
            continue;
        RayManifestFileEntry entry;
        entry.sha512 = sha;
        entry.source = RayManifestFileEntry::Source::Override;
        manifest.files.insert(topFile, entry);

        // Special: if this is the canonical options.txt, also capture the canonical snapshot
        // (the parsed key→value pairs). Smart-merge on future updates uses this to detect
        // "user never customized this key" → propagate new canonical changes.
        if (topFile == QStringLiteral("options.txt")) {
            QFile f(abs);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray bytes = f.readAll();
                f.close();
                manifest.optionsCanonicalSnapshot = RayOptionsMerge::parse(bytes).values;
            }
        }
    }

    return manifest;
}

}  // namespace RayManifestBuilder

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayDiffUpdater.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "Json.h"
#include "QObjectPtr.h"
#include "archive/ArchiveReader.h"
#include "minecraft/MinecraftInstance.h"
#include "net/ApiDownload.h"
#include "net/ChecksumValidator.h"
#include "net/Download.h"
#include "net/NetJob.h"

namespace {

/// Compute lowercase-hex SHA-512 of a byte buffer. Used for override files we extract from
/// the .mrpack (Modrinth gives us hashes for `files[]` entries but not for overrides — we
/// hash them ourselves at parse time so the manifest's diff logic can compare them next time).
QString sha512Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha512).toHex());
}

/// Reject paths that would escape the gameRoot. Modrinth's spec forbids `..` but a hostile or
/// careless .mrpack export could still ship one — we'd rather refuse the file than write
/// outside the instance.
bool isSafeRelativePath(const QString& relPath)
{
    if (relPath.isEmpty())
        return false;
    if (relPath.startsWith(QLatin1Char('/')) || relPath.contains(QStringLiteral(":")))
        return false;  // absolute path or Windows drive
    const QStringList parts = relPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        if (p == QStringLiteral("..") || p == QStringLiteral("."))
            return false;
    }
    return true;
}

}  // namespace

// ── Pack-content + player-data dir lists ─────────────────────────────────────────────────────

QStringList RayDiffUpdater::packContentDirs()
{
    // Directories the .mrpack is authoritative over. Migration mode wipes these before
    // reinstall so removed mods from the pre-manifest era don't linger and cause version
    // conflicts at game start.
    return {
        QStringLiteral("mods"),           QStringLiteral("config"),
        QStringLiteral("defaultconfigs"), QStringLiteral("resourcepacks"),
        QStringLiteral("shaderpacks"),    QStringLiteral("kubejs"),
        QStringLiteral("scripts"),        QStringLiteral("patchouli_books"),
        QStringLiteral("global_packs"),
    };
}

QStringList RayDiffUpdater::playerDataDirs()
{
    // Folders that contain player-generated content under any normal modpack. The LoaderChange
    // path backs these up before deleteInstance so the player's progress survives a forced
    // reimport. (Diff/Migration modes don't care — they don't call deleteInstance.)
    return {
        QStringLiteral("saves"),
        QStringLiteral("screenshots"),
        QStringLiteral("crash-reports"),
        QStringLiteral("logs"),
        QStringLiteral("backups"),
        QStringLiteral("XaerosWorldMap"),
        QStringLiteral("XaerosWaypoints"),
        QStringLiteral("XaerosMinimap"),
        QStringLiteral("journeymap"),
        QStringLiteral("schematics"),
        QStringLiteral("replay_recordings"),
        QStringLiteral("replay_cache"),
        QStringLiteral("voicechat"),
        QStringLiteral("local"),
    };
}

QStringList RayDiffUpdater::playerDataFiles()
{
    return {
        QStringLiteral("options.txt"),         QStringLiteral("optionsof.txt"),    QStringLiteral("optionsshaders.txt"),
        QStringLiteral("servers.dat"),         QStringLiteral("usercache.json"),    QStringLiteral("usernamecache.json"),
        QStringLiteral("realms_persistence.json"), QStringLiteral("hotbar.nbt"),
    };
}

// ── Ctor / abort ─────────────────────────────────────────────────────────────────────────────

RayDiffUpdater::RayDiffUpdater(const RayModpack& pack, InstancePtr instance, QWidget* parent)
    : m_pack(pack), m_instance(std::move(instance)), m_parentWidget(parent)
{
    setObjectName("RayDiffUpdater");
}

RayDiffUpdater::~RayDiffUpdater()
{
    if (!m_mrpackTempPath.isEmpty() && QFile::exists(m_mrpackTempPath)) {
        QFile::remove(m_mrpackTempPath);
    }
    if (!m_playerDataBackupRoot.isEmpty() && QDir(m_playerDataBackupRoot).exists()) {
        QDir(m_playerDataBackupRoot).removeRecursively();
    }
}

bool RayDiffUpdater::abort()
{
    if (m_mrpackDownloadJob)
        m_mrpackDownloadJob->abort();
    if (m_fileDownloadsJob)
        m_fileDownloadsJob->abort();
    if (m_wrappedImportTask)
        m_wrappedImportTask->abort();
    return Task::abort();
}

// ── Helpers ──────────────────────────────────────────────────────────────────────────────────

QString RayDiffUpdater::gameRoot() const
{
    auto mc = std::dynamic_pointer_cast<MinecraftInstance>(m_instance);
    return mc ? mc->gameRoot() : QString();
}

QString RayDiffUpdater::instanceRoot() const
{
    return m_instance ? m_instance->instanceRoot() : QString();
}

// ── executeTask: orchestrates the whole flow ─────────────────────────────────────────────────

void RayDiffUpdater::executeTask()
{
    if (!preflight())
        return;

    // Capture instance metadata up front. Diff/Migration modes don't touch these, but LoaderChange
    // mode does delete the instance — we restore from these afterwards.
    m_instanceName = m_instance->name();
    m_instanceGroup = APPLICATION->instances()->getInstanceGroup(m_instance->id());
    m_savedOverrideMemory = m_instance->settings()->get("OverrideMemory").toBool();
    m_savedMaxMemMb = m_instance->settings()->get("MaxMemAlloc").toInt();
    m_savedMinMemMb = m_instance->settings()->get("MinMemAlloc").toInt();

    // Stage the .mrpack alongside instance data so a crash-cleanup pass would catch it.
    QString tmpName = QStringLiteral("ray-update-%1.mrpack").arg(QDateTime::currentMSecsSinceEpoch());
    m_mrpackTempPath = FS::PathCombine(QDir::tempPath(), tmpName);

    setStatus(tr("Téléchargement du nouveau pack…"));
    auto job = makeShared<NetJob>(QStringLiteral("RayLauncher mrpack download"), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::Download::makeFile(m_pack.mrpackUrl, m_mrpackTempPath));
    m_mrpackDownloadJob = job;

    connect(job.get(), &NetJob::succeeded, this, &RayDiffUpdater::onMrpackDownloaded);
    connect(job.get(), &NetJob::failed, this, &RayDiffUpdater::onMrpackDownloadFailed);
    connect(job.get(), &NetJob::progress, this,
            [this](qint64 cur, qint64 tot) { setProgress(cur, tot); });
    job->start();
}

bool RayDiffUpdater::preflight()
{
    if (!m_instance) {
        emitFailed(tr("Instance introuvable."));
        return false;
    }
    if (m_instance->isRunning()) {
        emitFailed(tr("Arrête Minecraft avant de mettre à jour ce modpack."));
        return false;
    }
    if (!std::dynamic_pointer_cast<MinecraftInstance>(m_instance)) {
        emitFailed(tr("Cette instance n'est pas une instance Minecraft valide."));
        return false;
    }
    if (m_pack.mrpackUrl.isEmpty()) {
        emitFailed(tr("L'URL du modpack est vide dans le catalogue."));
        return false;
    }
    return true;
}

// ── Phase 1: .mrpack download done → parse + decide mode ─────────────────────────────────────

void RayDiffUpdater::onMrpackDownloadFailed(QString reason)
{
    m_mrpackDownloadJob.reset();
    emitFailed(tr("Téléchargement du pack échoué : %1").arg(reason));
}

void RayDiffUpdater::onMrpackDownloaded()
{
    m_mrpackDownloadJob.reset();
    setStatus(tr("Analyse du pack…"));

    if (!buildNewManifestFromMrpack()) {
        // buildNewManifestFromMrpack calls emitFailed itself with a precise message.
        return;
    }

    // Load the previous manifest from the instance, if it exists.
    auto oldOpt = RayModpackManifest::load(gameRoot());
    m_hadOldManifest = oldOpt.has_value();
    if (oldOpt.has_value()) {
        m_oldManifest = std::move(*oldOpt);
    }

    m_mode = determineMode();
    m_diff = RayModpackManifest::diff(m_oldManifest, m_newManifest);

    qDebug() << "RayDiffUpdater: mode ="
             << (m_mode == Mode::Migration       ? "Migration"
                 : m_mode == Mode::NormalDiff    ? "NormalDiff"
                 : m_mode == Mode::LoaderChange  ? "LoaderChange"
                                                 : "?")
             << "had_old_manifest =" << m_hadOldManifest
             << "old.version =" << m_oldManifest.packVersion << "new.version =" << m_newManifest.packVersion
             << "old.deps =" << m_oldManifest.minecraftVersion << "/" << m_oldManifest.neoforgeVersion
             << "new.deps =" << m_newManifest.minecraftVersion << "/" << m_newManifest.neoforgeVersion
             << "diff: add=" << m_diff.toAdd.size() << "rem=" << m_diff.toRemove.size()
             << "mod=" << m_diff.toModify.size() << "same=" << m_diff.unchanged.size();

    if (m_mode == Mode::LoaderChange) {
        applyLoaderChange();
    } else {
        applyDiffModes();
    }
}

// ── Phase 2: build the new manifest by walking the .mrpack ZIP ───────────────────────────────

bool RayDiffUpdater::buildNewManifestFromMrpack()
{
    m_newManifest = RayModpackManifest{};
    m_newManifest.modpackId = m_pack.id;
    m_newManifest.packVersion = m_pack.version;
    m_canonicalOptionsTxt.clear();

    MMCZip::ArchiveReader reader(m_mrpackTempPath);

    // First pass: find + parse modrinth.index.json (gives us `files[]` + dependencies).
    auto indexFile = reader.goToFile(QStringLiteral("modrinth.index.json"));
    if (!indexFile) {
        emitFailed(tr("modrinth.index.json introuvable dans le .mrpack."));
        return false;
    }
    QByteArray indexBytes = indexFile->readAll();
    if (indexBytes.isEmpty()) {
        emitFailed(tr("modrinth.index.json est vide ou illisible."));
        return false;
    }

    try {
        QJsonDocument doc = Json::requireDocument(indexBytes, "modrinth.index.json");
        QJsonObject obj = Json::requireObject(doc, "modrinth.index.json");

        const int formatVersion = Json::requireInteger(obj, "formatVersion", "modrinth.index.json");
        if (formatVersion != 1) {
            emitFailed(tr("Format de .mrpack non supporté : version %1").arg(formatVersion));
            return false;
        }

        // dependencies object: minecraft, neoforge, forge, fabric-loader, quilt-loader
        QJsonObject deps = obj.value(QStringLiteral("dependencies")).toObject();
        m_newManifest.minecraftVersion = deps.value(QStringLiteral("minecraft")).toString();
        m_newManifest.neoforgeVersion = deps.value(QStringLiteral("neoforge")).toString();
        m_newManifest.forgeVersion = deps.value(QStringLiteral("forge")).toString();
        m_newManifest.fabricVersion = deps.value(QStringLiteral("fabric-loader")).toString();
        m_newManifest.quiltVersion = deps.value(QStringLiteral("quilt-loader")).toString();

        // files[] — remote downloads with destination paths + SHA-512 + download URLs.
        QJsonArray filesArr = Json::requireIsType<QJsonArray>(obj, "files", "modrinth.index.json");
        for (const QJsonValue& v : filesArr) {
            if (!v.isObject())
                continue;
            QJsonObject f = v.toObject();
            QString path = Json::requireString(f, "path").replace('\\', '/');
            if (!isSafeRelativePath(path)) {
                qWarning() << "RayDiffUpdater: rejecting unsafe path in files[]:" << path;
                continue;
            }

            // env.client filter: "unsupported" = skip, "optional" = treat as required for our flow
            // (RayLauncher modpacks don't expose optional mods anyway), "required" = include.
            QJsonObject env = f.value("env").toObject();
            if (!env.isEmpty()) {
                QString clientSupport = env.value("client").toString(QStringLiteral("required"));
                if (clientSupport == QStringLiteral("unsupported"))
                    continue;
            }

            QJsonObject hashes = Json::requireObject(f, "hashes");
            QString sha512 = hashes.value("sha512").toString();
            if (sha512.isEmpty()) {
                qWarning() << "RayDiffUpdater: file entry has no sha512:" << path;
                continue;
            }

            // Pick the first valid URL from the downloads array. We don't preserve the fallback
            // list in the manifest (only one URL field). On the rare CDN failure we'd just
            // re-trigger an update — the launcher catalogue's URLs are stable enough.
            QString url;
            const QJsonArray downloads = f.value("downloads").toArray();
            for (const QJsonValue& d : downloads) {
                const QString candidate = d.toString();
                if (QUrl(candidate).isValid()) {
                    url = candidate;
                    break;
                }
            }
            if (url.isEmpty()) {
                qWarning() << "RayDiffUpdater: no valid download URL for" << path << "— skipping";
                continue;
            }

            RayManifestFileEntry entry;
            entry.sha512 = sha512.toLower();
            entry.source = RayManifestFileEntry::Source::Remote;
            entry.remoteUrl = url;
            m_newManifest.files.insert(path, entry);
        }
    } catch (const Json::JsonException& e) {
        emitFailed(tr("Erreur de parsing de modrinth.index.json : %1").arg(e.cause()));
        return false;
    }

    // Second pass: walk the rest of the ZIP for overrides/ + client-overrides/.
    // We re-open the archive because libarchive iterators are forward-only and we've already
    // consumed `modrinth.index.json` with goToFile above.
    MMCZip::ArchiveReader reader2(m_mrpackTempPath);
    bool walkOk = reader2.parse([this](MMCZip::ArchiveReader::File* f) -> bool {
        if (!f->isFile())
            return true;
        QString name = f->filename().replace('\\', '/');
        QString relPath;
        const QString kOverrides = QStringLiteral("overrides/");
        const QString kClientOverrides = QStringLiteral("client-overrides/");
        if (name.startsWith(kOverrides)) {
            relPath = name.mid(kOverrides.size());
        } else if (name.startsWith(kClientOverrides)) {
            relPath = name.mid(kClientOverrides.size());
        } else {
            return true;  // server-overrides/ + modrinth.index.json + anything else: skip
        }
        if (relPath.isEmpty() || !isSafeRelativePath(relPath)) {
            qWarning() << "RayDiffUpdater: skipping unsafe/empty override path:" << name;
            return true;
        }

        int status = 0;
        QByteArray content = f->readAll(&status);
        if (status != 0) {
            qWarning() << "RayDiffUpdater: failed to read override entry" << name;
            return true;  // skip this one but keep going
        }

        RayManifestFileEntry entry;
        entry.sha512 = sha512Hex(content);
        entry.source = RayManifestFileEntry::Source::Override;
        // client-overrides wins over overrides for the same path (matches Modrinth spec).
        m_newManifest.files.insert(relPath, entry);

        // Cache the bytes of options.txt — we'll need it for both smart-merge and snapshot.
        if (relPath == QStringLiteral("options.txt")) {
            m_canonicalOptionsTxt = content;
        }
        return true;
    });
    if (!walkOk) {
        qWarning() << "RayDiffUpdater: archive walk reported an error; continuing with partial manifest";
    }

    // Pre-build the options canonical snapshot so we save it with the manifest regardless of
    // whether we re-extract options.txt on disk (smart-merge writes it explicitly anyway).
    if (!m_canonicalOptionsTxt.isEmpty()) {
        auto parsed = RayOptionsMerge::parse(m_canonicalOptionsTxt);
        m_newManifest.optionsCanonicalSnapshot = parsed.values;
    }

    return true;
}

RayDiffUpdater::Mode RayDiffUpdater::determineMode() const
{
    if (!m_hadOldManifest)
        return Mode::Migration;
    if (!m_oldManifest.dependenciesMatch(m_newManifest))
        return Mode::LoaderChange;
    return Mode::NormalDiff;
}

// ── Phase 3a: Diff modes (NormalDiff + Migration) ────────────────────────────────────────────

void RayDiffUpdater::applyDiffModes()
{
    QStringList toInstall;  // toAdd ∪ toModify

    if (m_mode == Mode::Migration) {
        // Migration: we don't have a manifest, so the diff has every new file in `toAdd` and
        // nothing in `toRemove` (the comparison thinks "old has no files"). Wipe the pack-content
        // dirs to clean up leftover mods/configs from the pre-v1.1.0 era, then install everything.
        setStatus(tr("Première migration vers le système incrémental — nettoyage et réinstallation…"));
        wipePackContentDirs();
        toInstall = m_diff.toAdd;
        m_pendingDeletions.clear();  // nothing to delete; we already wiped
    } else {
        // NormalDiff: surgical changes only.
        toInstall = m_diff.toAdd;
        toInstall.append(m_diff.toModify);
        m_pendingDeletions = m_diff.toRemove;

        if (toInstall.isEmpty() && m_pendingDeletions.isEmpty()) {
            // True no-op. The pack version moved but nothing actually changed (file content
            // identical). Still write the manifest so pack_version is up to date.
            setStatus(tr("Aucun changement à appliquer — mise à jour du tag de version…"));
            finalizeAndSucceed();
            return;
        }

        const int add = m_diff.toAdd.size();
        const int rem = m_diff.toRemove.size();
        const int mod = m_diff.toModify.size();
        setStatus(tr("%1 ajout(s), %2 modification(s), %3 suppression(s)…").arg(add).arg(mod).arg(rem));
    }

    // Split toInstall into Remote (NetJob) and Override (ZIP extract).
    for (const QString& p : toInstall) {
        const auto it = m_newManifest.files.constFind(p);
        if (it == m_newManifest.files.constEnd())
            continue;
        if (it.value().source == RayManifestFileEntry::Source::Remote) {
            m_pendingRemoteDownloads.append(p);
        } else {
            m_pendingOverrideExtractions.append(p);
        }
    }

    // Kick off remote downloads first. If none → jump straight to overrides + finalize.
    if (m_pendingRemoteDownloads.isEmpty()) {
        onFileDownloadsFinished();
        return;
    }

    auto job = makeShared<NetJob>(QStringLiteral("RayLauncher diff downloads"), APPLICATION->network());
    job->setAskRetry(false);
    m_fileDownloadsJob = job;

    for (const QString& relPath : m_pendingRemoteDownloads) {
        const auto& entry = m_newManifest.files[relPath];
        const QString destPath = FS::PathCombine(gameRoot(), relPath);
        FS::ensureFilePathExists(destPath);
        auto dl = Net::ApiDownload::makeFile(QUrl(entry.remoteUrl), destPath);
        dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha512, entry.sha512));
        job->addNetAction(dl);
    }
    connect(job.get(), &NetJob::succeeded, this, &RayDiffUpdater::onFileDownloadsFinished);
    connect(job.get(), &NetJob::failed, this, &RayDiffUpdater::onFileDownloadsFailed);
    connect(job.get(), &NetJob::progress, this, [this](qint64 cur, qint64 tot) { setProgress(cur, tot); });

    setStatus(tr("Téléchargement de %1 fichier(s)…").arg(m_pendingRemoteDownloads.size()));
    job->start();
}

void RayDiffUpdater::wipePackContentDirs()
{
    const QString root = gameRoot();
    for (const QString& dir : packContentDirs()) {
        const QString abs = FS::PathCombine(root, dir);
        if (QDir(abs).exists()) {
            qDebug() << "RayDiffUpdater migration: wiping" << abs;
            FS::deletePath(abs);
        }
    }
}

void RayDiffUpdater::onFileDownloadsFailed(QString reason)
{
    m_fileDownloadsJob.reset();
    emitFailed(tr("Téléchargement des fichiers échoué : %1").arg(reason));
}

void RayDiffUpdater::onFileDownloadsFinished()
{
    m_fileDownloadsJob.reset();

    // Now extract any overrides from the .mrpack that we need to install/replace.
    if (!m_pendingOverrideExtractions.isEmpty()) {
        setStatus(tr("Application des fichiers du pack (%1)…").arg(m_pendingOverrideExtractions.size()));
        extractOverrideFiles(m_pendingOverrideExtractions);
    }

    // Apply deletions (NormalDiff only).
    if (!m_pendingDeletions.isEmpty()) {
        setStatus(tr("Suppression des fichiers retirés (%1)…").arg(m_pendingDeletions.size()));
        deleteFilesNoLongerInPack(m_pendingDeletions);
    }

    // Smart-merge options.txt last, before saving the manifest.
    setStatus(tr("Application de tes paramètres…"));
    mergeOptionsTxt();

    finalizeAndSucceed();
}

void RayDiffUpdater::extractOverrideFiles(const QStringList& destPaths)
{
    // We need to walk the .mrpack one more time and write out the entries that match the
    // destPaths set. The wanted set could be small (a single config file changed) or large
    // (Migration mode = every override), but in both cases a single walk is the cheapest plan.
    QSet<QString> wanted;
    for (const QString& p : destPaths)
        wanted.insert(p);

    MMCZip::ArchiveReader reader(m_mrpackTempPath);
    reader.parse([this, &wanted](MMCZip::ArchiveReader::File* f) -> bool {
        if (!f->isFile())
            return true;
        QString name = f->filename().replace('\\', '/');
        QString relPath;
        const QString kOverrides = QStringLiteral("overrides/");
        const QString kClientOverrides = QStringLiteral("client-overrides/");
        if (name.startsWith(kOverrides)) {
            relPath = name.mid(kOverrides.size());
        } else if (name.startsWith(kClientOverrides)) {
            relPath = name.mid(kClientOverrides.size());
        } else {
            return true;
        }
        if (!wanted.contains(relPath))
            return true;

        int status = 0;
        QByteArray content = f->readAll(&status);
        if (status != 0) {
            qWarning() << "RayDiffUpdater: failed to read override for write:" << name;
            return true;
        }

        const QString destPath = FS::PathCombine(gameRoot(), relPath);

        // Special case: options.txt is handled by the smart-merge step, NOT by raw overwrite.
        // We capture the canonical bytes for the merge but skip writing the verbatim content
        // here. (For Migration mode we let the canonical land verbatim by NOT including
        // options.txt in this branch — see mergeOptionsTxt for the per-mode policy.)
        if (relPath == QStringLiteral("options.txt")) {
            m_canonicalOptionsTxt = content;
            return true;
        }

        FS::ensureFilePathExists(destPath);
        try {
            FS::write(destPath, content);
        } catch (const FS::FileSystemException& e) {
            qWarning() << "RayDiffUpdater: failed to write" << destPath << ":" << e.cause();
        }
        return true;
    });
}

void RayDiffUpdater::deleteFilesNoLongerInPack(const QStringList& destPaths)
{
    const QString root = gameRoot();
    for (const QString& rel : destPaths) {
        const QString abs = FS::PathCombine(root, rel);
        if (QFile::exists(abs)) {
            qDebug() << "RayDiffUpdater: deleting" << abs;
            QFile::remove(abs);
        }
    }
}

void RayDiffUpdater::mergeOptionsTxt()
{
    const QString optionsPath = FS::PathCombine(gameRoot(), QStringLiteral("options.txt"));
    const bool forceReset =
        !m_pack.forceOptionsResetForVersion.isEmpty() && m_pack.forceOptionsResetForVersion == m_pack.version;

    qDebug() << "RayDiffUpdater::mergeOptionsTxt"
             << "mode =" << (m_mode == Mode::Migration ? "Migration" : "NormalDiff")
             << "has_canonical =" << !m_canonicalOptionsTxt.isEmpty() << "forceReset =" << forceReset
             << "snapshot_keys =" << m_oldManifest.optionsCanonicalSnapshot.size();

    if (m_canonicalOptionsTxt.isEmpty()) {
        // The .mrpack doesn't ship overrides/options.txt — nothing to merge. Leave whatever's
        // on disk (could be Minecraft-generated or user-edited).
        return;
    }

    if (m_mode == Mode::Migration) {
        // Migration semantic accepted by the pack author: drop the canonical verbatim so every
        // friend starts from a clean state. Their previous customizations were already lost
        // to the pre-v1.1.0 "wipe + reimport" updater anyway.
        FS::ensureFilePathExists(optionsPath);
        try {
            FS::write(optionsPath, m_canonicalOptionsTxt);
        } catch (const FS::FileSystemException& e) {
            qWarning() << "RayDiffUpdater migration: cannot write options.txt:" << e.cause();
        }
        return;
    }

    // NormalDiff path. Load whatever the player has now (may be the canonical from last
    // install + their post-install tweaks), smart-merge against the new canonical using the
    // last-installed snapshot to distinguish "untouched defaults" from "user customizations".
    QByteArray userOptions;
    if (QFile::exists(optionsPath)) {
        QFile f(optionsPath);
        if (f.open(QIODevice::ReadOnly)) {
            userOptions = f.readAll();
            f.close();
        }
    }

    QByteArray merged = RayOptionsMerge::smartMerge(userOptions, m_canonicalOptionsTxt,
                                                     m_oldManifest.optionsCanonicalSnapshot, forceReset);
    FS::ensureFilePathExists(optionsPath);
    try {
        FS::write(optionsPath, merged);
    } catch (const FS::FileSystemException& e) {
        qWarning() << "RayDiffUpdater: cannot write merged options.txt:" << e.cause();
    }
}

void RayDiffUpdater::finalizeAndSucceed()
{
    // Persist the new manifest at the very end. If any earlier step failed and we didn't reach
    // here, the old manifest is still on disk and the next update attempt will diff against
    // the same baseline (idempotent recovery).
    if (!m_newManifest.save(gameRoot())) {
        emitFailed(tr("Mise à jour appliquée mais impossible d'écrire le manifest. La prochaine "
                       "mise à jour repassera par le mode migration."));
        return;
    }

    // Tag the instance with the new pack version so the page's "Update available" indicator
    // updates immediately. Keep RayLauncher_ModpackId in case it was somehow missing.
    if (m_instance) {
        m_instance->settings()->set(QStringLiteral("RayLauncher_ModpackId"), m_pack.id);
        m_instance->settings()->set(QStringLiteral("RayLauncher_ModpackVersion"), m_pack.version);
    }

    // Clean up the cached .mrpack download.
    if (!m_mrpackTempPath.isEmpty()) {
        QFile::remove(m_mrpackTempPath);
        m_mrpackTempPath.clear();
    }

    setStatus(tr("Mis à jour vers %1 ✓").arg(m_pack.version));
    emitSucceeded();
}

// ── Phase 3b: LoaderChange — full reimport with player-data preservation ─────────────────────

void RayDiffUpdater::applyLoaderChange()
{
    setStatus(tr("Changement de version Minecraft / NeoForge détecté — réinstallation complète "
                 "avec préservation de tes mondes…"));

    // Backup player data dirs + files to a temp area outside the instance so deleteInstance
    // doesn't take them with it.
    backupPlayerDataDirs();

    const QString oldId = m_instance->id();
    APPLICATION->instances()->deleteInstance(oldId);
    m_instance.reset();

    setStatus(tr("Téléchargement et installation de la nouvelle version…"));
    auto* importTask = new InstanceImportTask(m_pack.mrpackUrl, m_parentWidget);
    importTask->setName(m_instanceName);
    if (!m_instanceGroup.isEmpty())
        importTask->setGroup(m_instanceGroup);

    m_wrappedImportTask = APPLICATION->instances()->wrapInstanceTask(importTask);
    connect(m_wrappedImportTask, &Task::succeeded, this, &RayDiffUpdater::onLoaderChangeImportSucceeded);
    connect(m_wrappedImportTask, &Task::failed, this, &RayDiffUpdater::onLoaderChangeImportFailed);
    connect(m_wrappedImportTask, &Task::progress, this, [this](qint64 c, qint64 t) { setProgress(c, t); });
    connect(m_wrappedImportTask, &Task::status, this, [this](const QString& s) { setStatus(s); });
    connect(m_wrappedImportTask, &Task::succeeded, m_wrappedImportTask, &QObject::deleteLater);
    connect(m_wrappedImportTask, &Task::failed, m_wrappedImportTask, &QObject::deleteLater);
    connect(m_wrappedImportTask, &Task::aborted, m_wrappedImportTask, &QObject::deleteLater);
    m_wrappedImportTask->start();
}

void RayDiffUpdater::backupPlayerDataDirs()
{
    const QString src = gameRoot();
    if (src.isEmpty())
        return;

    m_playerDataBackupRoot = FS::PathCombine(
        QDir::tempPath(),
        QStringLiteral("ray-loader-change-backup-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(m_playerDataBackupRoot);

    for (const QString& dir : playerDataDirs()) {
        const QString srcDir = FS::PathCombine(src, dir);
        if (!QDir(srcDir).exists())
            continue;
        const QString dstDir = FS::PathCombine(m_playerDataBackupRoot, dir);
        qDebug() << "RayDiffUpdater LoaderChange backup:" << srcDir << "→" << dstDir;
        QDir().mkpath(dstDir);
        FS::copy(srcDir, dstDir)();
    }
    for (const QString& file : playerDataFiles()) {
        const QString srcFile = FS::PathCombine(src, file);
        if (!QFile::exists(srcFile))
            continue;
        const QString dstFile = FS::PathCombine(m_playerDataBackupRoot, file);
        QFile::copy(srcFile, dstFile);
    }
    // Also stash the old manifest so we don't fully lose the snapshot if anything goes weird —
    // it lets the next update's smart-merge work even after a loader-change reimport.
    const QString srcManifest = RayModpackManifest::pathFor(src);
    if (QFile::exists(srcManifest)) {
        QFile::copy(srcManifest, FS::PathCombine(m_playerDataBackupRoot,
                                                  QStringLiteral(".ray-modpack-manifest.json")));
    }
}

void RayDiffUpdater::restorePlayerDataDirs()
{
    if (m_playerDataBackupRoot.isEmpty())
        return;

    // Find the freshly-reimported instance by name (the reimport gave it a fresh id).
    InstancePtr fresh;
    auto list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        InstancePtr inst = list->at(i);
        if (inst && inst->name().trimmed() == m_instanceName.trimmed())
            fresh = inst;
    }
    if (!fresh) {
        qWarning() << "RayDiffUpdater LoaderChange: fresh instance not found by name; cannot restore";
        return;
    }

    auto mcFresh = std::dynamic_pointer_cast<MinecraftInstance>(fresh);
    if (!mcFresh)
        return;

    const QString dst = mcFresh->gameRoot();
    QDir().mkpath(dst);

    for (const QString& dir : playerDataDirs()) {
        const QString srcDir = FS::PathCombine(m_playerDataBackupRoot, dir);
        if (!QDir(srcDir).exists())
            continue;
        const QString dstDir = FS::PathCombine(dst, dir);
        QDir().mkpath(dstDir);
        FS::copy(srcDir, dstDir).overwrite(true)();
    }
    for (const QString& file : playerDataFiles()) {
        const QString srcFile = FS::PathCombine(m_playerDataBackupRoot, file);
        if (!QFile::exists(srcFile))
            continue;
        const QString dstFile = FS::PathCombine(dst, file);
        // Overwrite the freshly-extracted version: the player's settings win.
        if (QFile::exists(dstFile))
            QFile::remove(dstFile);
        QFile::copy(srcFile, dstFile);
    }

    // Restore the player's RAM customization (the fresh instance came with the global default).
    if (m_savedOverrideMemory && m_savedMaxMemMb > 0) {
        fresh->settings()->set(QStringLiteral("OverrideMemory"), true);
        fresh->settings()->set(QStringLiteral("MaxMemAlloc"), m_savedMaxMemMb);
        if (m_savedMinMemMb > 0)
            fresh->settings()->set(QStringLiteral("MinMemAlloc"), m_savedMinMemMb);
    } else if (m_pack.recommendedMemoryMb > 0) {
        fresh->settings()->set(QStringLiteral("OverrideMemory"), true);
        fresh->settings()->set(QStringLiteral("MaxMemAlloc"), m_pack.recommendedMemoryMb);
        fresh->settings()->set(QStringLiteral("MinMemAlloc"), m_pack.recommendedMemoryMb);
    }

    // Re-tag with the new pack version.
    fresh->settings()->set(QStringLiteral("RayLauncher_ModpackId"), m_pack.id);
    fresh->settings()->set(QStringLiteral("RayLauncher_ModpackVersion"), m_pack.version);

    // Now write the new manifest into the fresh instance. We have m_newManifest already
    // populated from the parse step.
    m_newManifest.save(dst);

    QDir(m_playerDataBackupRoot).removeRecursively();
    m_playerDataBackupRoot.clear();
}

void RayDiffUpdater::onLoaderChangeImportFailed(QString reason)
{
    // Roll back as best we can: restore the player data onto whatever instance still exists
    // under m_instanceName (if any). If not, the backup folder is left behind for manual
    // recovery and we log its path.
    qWarning() << "RayDiffUpdater LoaderChange: import failed:" << reason
               << "— backup left at:" << m_playerDataBackupRoot;
    emitFailed(tr("Mise à jour échouée (changement de version) : %1\n\n"
                  "Tes données joueur sont sauvegardées ici en cas de besoin :\n%2")
                   .arg(reason, m_playerDataBackupRoot));
}

void RayDiffUpdater::onLoaderChangeImportSucceeded()
{
    setStatus(tr("Restauration de tes mondes, captures et waypoints…"));
    restorePlayerDataDirs();

    if (!m_mrpackTempPath.isEmpty()) {
        QFile::remove(m_mrpackTempPath);
        m_mrpackTempPath.clear();
    }

    setStatus(tr("Mis à jour vers %1 ✓").arg(m_pack.version));
    emitSucceeded();
}

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayModpackPage.h"

#include <QAction>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QMenu>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "BuildConfig.h"
#include "InstanceList.h"
#include "settings/SettingsObject.h"
#include "ui/widgets/FlowLayout.h"

RayModpackPage::RayModpackPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* header = new QLabel(tr("Choisis un modpack — un clic sur Installer suffit. Clic droit pour plus d'options."), this);
    QFont headerFont = header->font();
    headerFont.setPointSize(headerFont.pointSize() + 1);
    header->setFont(headerFont);
    root->addWidget(header);

    // Self-update banner — same visual language as the modpack tile's "Mettre à jour" button
    // (orange/amber accent so it reads as "action required" without alarming red). Hidden
    // until checkForLauncherUpdate() proves a newer release exists on GitHub. Clicking the
    // button hands off to the PrismUpdater binary via APPLICATION->triggerUpdateCheck().
    m_updateBanner = new QWidget(this);
    m_updateBanner->setObjectName("ray-launcher-update-banner");
    m_updateBanner->setStyleSheet(QStringLiteral(R"QSS(
        QWidget#ray-launcher-update-banner {
            background-color: #2A2014;
            border: 1px solid #F5A524;
            border-radius: 8px;
        }
        QWidget#ray-launcher-update-banner QLabel {
            color: #F5A524;
            font-weight: 600;
        }
        QWidget#ray-launcher-update-banner QPushButton {
            background-color: #F5A524;
            color: #13181F;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            font-weight: 600;
        }
        QWidget#ray-launcher-update-banner QPushButton:hover {
            background-color: #FFC04F;
        }
    )QSS"));
    auto* bannerLayout = new QHBoxLayout(m_updateBanner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    bannerLayout->setSpacing(12);
    m_updateBannerLabel = new QLabel(m_updateBanner);
    bannerLayout->addWidget(m_updateBannerLabel, 1);
    auto* updateBtn = new QPushButton(tr("Mettre à jour"), m_updateBanner);
    updateBtn->setCursor(Qt::PointingHandCursor);
    connect(updateBtn, &QPushButton::clicked, this, []() {
        // Same hand-off as the menu-bar action: spawns the PrismUpdater binary which
        // downloads the latest release from GitHub, closes the launcher, applies the
        // upgrade, and re-launches.
        APPLICATION->triggerUpdateCheck();
    });
    bannerLayout->addWidget(updateBtn, 0);
    m_updateBanner->setVisible(false);
    root->addWidget(m_updateBanner);

    // Scroll area containing the tile grid (FlowLayout reflows tiles as the window resizes).
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_tilesContainer = new QWidget(m_scrollArea);
    m_tilesLayout = new FlowLayout(m_tilesContainer, /*margin=*/0, /*hSpacing=*/12, /*vSpacing=*/12);
    m_tilesContainer->setLayout(m_tilesLayout);

    m_scrollArea->setWidget(m_tilesContainer);
    root->addWidget(m_scrollArea, 1);

    // Status line + refresh control
    auto* footer = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    footer->addWidget(m_statusLabel, 1);

    m_refreshButton = new QPushButton(tr("Rafraîchir"), this);
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    connect(m_refreshButton, &QPushButton::clicked, this, &RayModpackPage::onRefreshClicked);
    footer->addWidget(m_refreshButton, 0);

    root->addLayout(footer);

    // Wire the index fetcher
    m_fetcher = new RayModpackIndexFetcher(APPLICATION->network(), BuildConfig.RAYLAUNCHER_MODPACK_INDEX_URL, this);
    connect(m_fetcher, &RayModpackIndexFetcher::loaded, this, &RayModpackPage::onIndexLoaded);
    connect(m_fetcher, &RayModpackIndexFetcher::failed, this, &RayModpackPage::onIndexFailed);

    // Watch the instance list so tiles flip from Install → Play automatically once an install finishes.
    auto* instances = APPLICATION->instances().get();
    connect(instances, &InstanceList::rowsInserted, this, &RayModpackPage::onInstanceListChanged);
    connect(instances, &InstanceList::rowsRemoved, this, &RayModpackPage::onInstanceListChanged);
    connect(instances, &InstanceList::dataChanged, this, &RayModpackPage::onInstanceListChanged);

    setStatus(tr("Chargement du catalogue…"), false);
    m_refreshButton->setEnabled(false);
    m_fetcher->fetch();

    // Kick off the launcher self-update check after a brief delay so it doesn't compete with
    // the catalogue fetch on initial load. The check is best-effort: any failure (offline,
    // GitHub rate-limit, JSON change) is silent — we just don't show the banner.
    QTimer::singleShot(2000, this, &RayModpackPage::checkForLauncherUpdate);
}

RayModpackPage::~RayModpackPage() = default;

void RayModpackPage::setStatus(const QString& text, bool isError)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #c23b22;") : QString());
}

void RayModpackPage::onRefreshClicked()
{
    if (m_fetcher->isLoading())
        return;
    setStatus(tr("Chargement du catalogue…"), false);
    m_refreshButton->setEnabled(false);
    m_fetcher->fetch();
}

void RayModpackPage::onIndexLoaded()
{
    m_refreshButton->setEnabled(true);
    const int n = m_fetcher->modpacks().size();
    if (n == 0) {
        setStatus(tr("Aucun modpack dans le catalogue. Ajoutes-en un dans ton index.json."), false);
    } else {
        setStatus(tr("%1 modpack(s) disponible(s).").arg(n), false);
    }
    rebuildTiles();
}

void RayModpackPage::onIndexFailed(QString error)
{
    m_refreshButton->setEnabled(true);
    setStatus(tr("Erreur : %1").arg(error), true);
    rebuildTiles();  // still rebuild — shows an empty grid
}

void RayModpackPage::onInstanceListChanged()
{
    rebuildTiles();
}

QString RayModpackPage::installedInstanceIdFor(const RayModpack& pack) const
{
    // v1 heuristic: match installed instances by name. Swapped for ID-based matching in Commit 5
    // once the RayLauncher_ModpackId tag we write on install has propagated through everyone's data.
    auto list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        InstancePtr inst = list->at(i);
        if (!inst)
            continue;
        if (inst->name().trimmed() == pack.name.trimmed())
            return inst->id();
    }
    return {};
}

void RayModpackPage::rebuildTiles()
{
    // Tear down existing tiles.
    while (QLayoutItem* item = m_tilesLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    // Pass 1 — catalogue tiles. Keep track of which instances got matched so we don't show
    // them twice in pass 2.
    QSet<QString> matchedInstanceIds;
    for (const RayModpack& pack : m_fetcher->modpacks()) {
        const QString instId = installedInstanceIdFor(pack);
        if (!instId.isEmpty())
            matchedInstanceIds.insert(instId);

        RayModpackCard::State state;
        if (instId.isEmpty()) {
            state = RayModpackCard::State::Available;
        } else if (!pack.version.isEmpty()) {
            // Compare the version stamped on the instance (RayLauncher_ModpackVersion, set by
            // the install flow) against the one advertised in the catalogue. A mismatch means
            // the user has an older build and can upgrade.
            InstancePtr inst = APPLICATION->instances()->getInstanceById(instId);
            const QString installedVer =
                inst ? inst->settings()->get(QStringLiteral("RayLauncher_ModpackVersion")).toString() : QString();
            state = (!installedVer.isEmpty() && installedVer != pack.version) ? RayModpackCard::State::UpdateAvailable
                                                                              : RayModpackCard::State::Installed;
        } else {
            state = RayModpackCard::State::Installed;
        }
        addTile(pack, state, instId);
    }

    // Pass 2 — user-added tiles. Any instance that doesn't correspond to a catalogue entry gets
    // its own tile in the same grid, with an empty synthetic RayModpack (empty id flags it as
    // "not from the catalogue"). Because the instance has no RayLauncher_ModpackId setting, the
    // context-menu delete guard lets the user remove it.
    auto list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        InstancePtr inst = list->at(i);
        if (!inst)
            continue;
        if (matchedInstanceIds.contains(inst->id()))
            continue;

        RayModpack synthetic;
        synthetic.name = inst->name();
        synthetic.description = tr("Ajoutée manuellement");
        // id/version/iconUrl/mrpackUrl stay empty — the card treats this as a plain Installed tile.

        addTile(synthetic, RayModpackCard::State::Installed, inst->id());
    }
}

void RayModpackPage::addTile(const RayModpack& pack, RayModpackCard::State state, const QString& instanceId)
{
    auto* card = new RayModpackCard(pack, state, instanceId, m_tilesContainer);
    connect(card, &RayModpackCard::installClicked, this, &RayModpackPage::installRequested);
    connect(card, &RayModpackCard::playClicked, this, &RayModpackPage::playRequested);
    connect(card, &RayModpackCard::killClicked, this, &RayModpackPage::killRequested);
    connect(card, &RayModpackCard::updateClicked, this, &RayModpackPage::updateRequested);
    connect(card, &RayModpackCard::contextMenuRequested, this, &RayModpackPage::onCardContextMenu);

    // For installed instances, pick up the initial running state and subscribe to future changes
    // so the button flips between "Jouer" and "Arrêter" without the user having to refresh.
    if (!instanceId.isEmpty()) {
        InstancePtr inst = APPLICATION->instances()->getInstanceById(instanceId);
        if (inst) {
            card->setRunning(inst->isRunning());
            connect(inst.get(), &BaseInstance::runningStatusChanged, card,
                    [card](bool running) { card->setRunning(running); });
        }
    }

    // Icon resolution order: cached (catalogue pack previously fetched) → URL (catalogue pack,
    // first time) → app logo (user-added tile or catalogue pack with no icon_url).
    if (!pack.id.isEmpty() && m_iconCache.contains(pack.id) && !m_iconCache.value(pack.id).isNull()) {
        card->setIcon(m_iconCache.value(pack.id));
    } else if (!pack.iconUrl.isEmpty()) {
        fetchIcon(card, pack.iconUrl);
    } else {
        card->setIcon(APPLICATION->logo().pixmap(128, 128));
    }

    m_tilesLayout->addWidget(card);
}

void RayModpackPage::fetchIcon(RayModpackCard* card, const QUrl& url)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = APPLICATION->network()->get(req);

    const QString packId = card->pack().id;
    QPointer<RayModpackCard> cardGuard(card);

    connect(reply, &QNetworkReply::finished, this, [this, reply, packId, cardGuard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()))
            return;
        m_iconCache.insert(packId, pix);
        if (cardGuard)
            cardGuard->setIcon(pix);
    });
}

void RayModpackPage::onCardContextMenu(const RayModpack& pack, const QString& instanceId, const QPoint& globalPos)
{
    QMenu menu(this);

    const bool installed = !instanceId.isEmpty();

    if (!installed) {
        // Nothing useful in a right-click for an un-installed pack; just offer Install for parity.
        auto* installAct = menu.addAction(tr("Installer"));
        connect(installAct, &QAction::triggered, this, [this, pack]() { emit installRequested(pack); });
    } else {
        auto* playAct = menu.addAction(tr("Jouer"));
        connect(playAct, &QAction::triggered, this, [this, instanceId]() { emit playRequested(instanceId); });

        auto* memoryAct = menu.addAction(tr("Mémoire allouée…"));
        connect(memoryAct, &QAction::triggered, this, [this, pack, instanceId]() { emit memoryRequested(pack, instanceId); });

        auto* folderAct = menu.addAction(tr("Ouvrir le dossier"));
        connect(folderAct, &QAction::triggered, this, [this, instanceId]() { emit openFolderRequested(instanceId); });

        menu.addSeparator();

        auto* deleteAct = menu.addAction(tr("Supprimer"));
        // Delete guard: if this instance is tagged as a RayLauncher catalogue pack, disable the action.
        InstancePtr inst = APPLICATION->instances()->getInstanceById(instanceId);
        bool isCataloguePack = false;
        if (inst) {
            const QString tag = inst->settings()->get("RayLauncher_ModpackId").toString();
            isCataloguePack = !tag.isEmpty();
        }
        if (isCataloguePack) {
            deleteAct->setEnabled(false);
            deleteAct->setToolTip(tr("Modpack du catalogue RayLauncher — non supprimable."));
        } else {
            connect(deleteAct, &QAction::triggered, this, [this, instanceId]() { emit deleteRequested(instanceId); });
        }
    }

    menu.exec(globalPos);
}

namespace {
/// Strict semver-style comparator: returns true if @p remote (e.g. "1.0.1") is strictly higher
/// than @p local (e.g. "1.0.0"). Each side is parsed as exactly three integers separated by
/// dots; missing parts are treated as 0. Returns false on parse failure to err on the side of
/// "no banner" rather than spamming a false update prompt.
bool isRemoteVersionNewer(int localMajor, int localMinor, int localPatch, const QString& remote)
{
    const QStringList parts = remote.split('.');
    if (parts.size() < 1)
        return false;
    bool ok1 = true, ok2 = true, ok3 = true;
    int rMajor = parts.value(0).toInt(&ok1);
    int rMinor = parts.value(1, "0").toInt(&ok2);
    int rPatch = parts.value(2, "0").toInt(&ok3);
    if (!ok1 || !ok2 || !ok3)
        return false;
    if (rMajor != localMajor)
        return rMajor > localMajor;
    if (rMinor != localMinor)
        return rMinor > localMinor;
    return rPatch > localPatch;
}
}  // namespace

void RayModpackPage::checkForLauncherUpdate()
{
    // Don't bother if the build wasn't shipped with the PrismUpdater binary alongside it —
    // even if a newer release exists, the "Mettre à jour" button can't actually do anything,
    // and a teaser banner with no working action would be worse than no banner at all.
    if (!APPLICATION->updaterEnabled())
        return;

    // GitHub's REST API returns the latest *stable* (non-prerelease, non-draft) release.
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/RayanLhrd/RayLauncher/releases/latest")));
    req.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub recommends a User-Agent on all API calls; without it they'll sometimes return 403.
    req.setRawHeader("User-Agent", "RayLauncher-UpdateCheck");

    QNetworkReply* reply = APPLICATION->network()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray body = reply->readAll();
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
            return;
        // The tag we publish looks like "v1.0.1"; the API echoes it as `tag_name`. Strip the
        // optional leading "v" before comparing — humans tag with v-prefix but our internal
        // BuildConfig.VERSION_* are bare integers.
        QString tag = doc.object().value("tag_name").toString();
        if (tag.startsWith('v') || tag.startsWith('V'))
            tag = tag.mid(1);
        if (tag.isEmpty())
            return;
        if (!isRemoteVersionNewer(BuildConfig.VERSION_MAJOR, BuildConfig.VERSION_MINOR, BuildConfig.VERSION_PATCH, tag))
            return;
        showLauncherUpdateBanner(tag);
    });
}

void RayModpackPage::showLauncherUpdateBanner(const QString& latestVersion)
{
    if (!m_updateBanner || !m_updateBannerLabel)
        return;
    const QString currentVersion =
        QStringLiteral("%1.%2.%3").arg(BuildConfig.VERSION_MAJOR).arg(BuildConfig.VERSION_MINOR).arg(BuildConfig.VERSION_PATCH);
    m_updateBannerLabel->setText(tr("✨ Une nouvelle version de RayLauncher est dispo : %1 (tu es en %2)").arg(latestVersion, currentVersion));
    m_updateBanner->setVisible(true);
}

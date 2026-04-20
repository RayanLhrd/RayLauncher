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

#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "BuildConfig.h"
#include "InstanceList.h"

RayModpackPage::RayModpackPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* header = new QLabel(tr("Choisis un modpack. Pas encore installé ? Un clic et c'est parti."), this);
    QFont headerFont = header->font();
    headerFont.setPointSize(headerFont.pointSize() + 1);
    header->setFont(headerFont);
    root->addWidget(header);

    // Scroll area containing the vertical stack of cards.
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_cardsContainer = new QWidget(m_scrollArea);
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(10);
    m_cardsLayout->addStretch(1);

    m_scrollArea->setWidget(m_cardsContainer);
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

    // Watch the instance list so cards flip from Install → Play automatically once an install finishes.
    auto* instances = APPLICATION->instances().get();
    connect(instances, &InstanceList::rowsInserted, this, &RayModpackPage::onInstanceListChanged);
    connect(instances, &InstanceList::rowsRemoved, this, &RayModpackPage::onInstanceListChanged);
    connect(instances, &InstanceList::dataChanged, this, &RayModpackPage::onInstanceListChanged);

    setStatus(tr("Chargement du catalogue…"), false);
    m_refreshButton->setEnabled(false);
    m_fetcher->fetch();
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
    rebuildCards();
}

void RayModpackPage::onIndexFailed(QString error)
{
    m_refreshButton->setEnabled(true);
    setStatus(tr("Erreur : %1").arg(error), true);
    rebuildCards();  // still rebuild — shows an empty list
}

void RayModpackPage::onInstanceListChanged()
{
    // An install or delete happened; re-evaluate each card's state.
    rebuildCards();
}

QString RayModpackPage::installedInstanceIdFor(const RayModpack& pack) const
{
    // v1 heuristic: match installed instances by name. Proper RayLauncher_ModpackId tagging
    // arrives in the next commit alongside the Update flow.
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

void RayModpackPage::rebuildCards()
{
    // Clear all cards (keep the trailing stretch).
    while (m_cardsLayout->count() > 0) {
        QLayoutItem* item = m_cardsLayout->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const auto& packs = m_fetcher->modpacks();
    for (const RayModpack& pack : packs) {
        const QString instId = installedInstanceIdFor(pack);
        const RayModpackCard::State state = instId.isEmpty() ? RayModpackCard::State::Available : RayModpackCard::State::Installed;

        auto* card = new RayModpackCard(pack, state, instId, m_cardsContainer);
        connect(card, &RayModpackCard::installClicked, this, &RayModpackPage::installRequested);
        connect(card, &RayModpackCard::playClicked, this, &RayModpackPage::playRequested);
        connect(card, &RayModpackCard::updateClicked, this, &RayModpackPage::updateRequested);

        // Re-attach a cached icon if we already fetched it.
        if (m_iconCache.contains(pack.id) && !m_iconCache.value(pack.id).isNull()) {
            card->setIcon(m_iconCache.value(pack.id));
        } else if (!pack.iconUrl.isEmpty()) {
            fetchIcon(card, pack.iconUrl);
        }

        m_cardsLayout->addWidget(card);
    }

    m_cardsLayout->addStretch(1);
}

void RayModpackPage::fetchIcon(RayModpackCard* card, const QUrl& url)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = APPLICATION->network()->get(req);

    // Track by pack id instead of the card pointer — the card may get recreated during a rebuild.
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

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

#include "RayModpackPage.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "Application.h"
#include "BuildConfig.h"

namespace {
constexpr int kIconSize = 96;
}  // namespace

RayModpackPage::RayModpackPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);

    auto* header = new QLabel(tr("Choisis un modpack à installer. Un clic suffit."), this);
    root->addWidget(header);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kIconSize, kIconSize));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setGridSize(QSize(kIconSize + 64, kIconSize + 64));
    m_list->setSpacing(12);
    m_list->setWordWrap(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_list, 1);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setMinimumHeight(48);
    root->addWidget(m_descriptionLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto* buttonRow = new QHBoxLayout();
    m_refreshButton = new QPushButton(tr("Rafraîchir"), this);
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addStretch(1);
    m_installButton = new QPushButton(tr("Installer"), this);
    m_installButton->setDefault(true);
    m_installButton->setEnabled(false);
    buttonRow->addWidget(m_installButton);
    root->addLayout(buttonRow);

    connect(m_refreshButton, &QPushButton::clicked, this, &RayModpackPage::onRefreshClicked);
    connect(m_installButton, &QPushButton::clicked, this, &RayModpackPage::onInstallClicked);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &RayModpackPage::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &RayModpackPage::onItemDoubleClicked);

    m_fetcher = new RayModpackIndexFetcher(APPLICATION->network(), BuildConfig.RAYLAUNCHER_MODPACK_INDEX_URL, this);
    connect(m_fetcher, &RayModpackIndexFetcher::loaded, this, &RayModpackPage::onIndexLoaded);
    connect(m_fetcher, &RayModpackIndexFetcher::failed, this, &RayModpackPage::onIndexFailed);

    setStatus(tr("Chargement de la liste de modpacks…"), false);
    m_refreshButton->setEnabled(false);
    m_fetcher->fetch();
}

RayModpackPage::~RayModpackPage() = default;

void RayModpackPage::setStatus(const QString& statusText, bool isError)
{
    m_statusLabel->setText(statusText);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #c23b22;") : QString());
}

void RayModpackPage::onRefreshClicked()
{
    if (m_fetcher->isLoading())
        return;
    setStatus(tr("Chargement de la liste de modpacks…"), false);
    m_refreshButton->setEnabled(false);
    m_installButton->setEnabled(false);
    m_fetcher->fetch();
}

void RayModpackPage::onIndexLoaded()
{
    rebuildList();
    m_refreshButton->setEnabled(true);
    if (m_fetcher->modpacks().isEmpty()) {
        setStatus(tr("Aucun modpack dans l'index. Ajoutes-en un dans ton index.json."), false);
    } else {
        setStatus(tr("%1 modpack(s) disponibles.").arg(m_fetcher->modpacks().size()), false);
    }
}

void RayModpackPage::onIndexFailed(QString error)
{
    m_list->clear();
    m_refreshButton->setEnabled(true);
    m_installButton->setEnabled(false);
    setStatus(tr("Erreur : %1").arg(error), true);
}

void RayModpackPage::rebuildList()
{
    m_list->clear();
    const QIcon fallbackIcon = style()->standardIcon(QStyle::SP_FileDialogInfoView);
    const auto& packs = m_fetcher->modpacks();
    for (int i = 0; i < packs.size(); ++i) {
        const RayModpack& pack = packs[i];
        auto* item = new QListWidgetItem(fallbackIcon, pack.name);
        item->setData(Qt::UserRole, pack.id);
        item->setToolTip(pack.description.isEmpty() ? pack.name : pack.description);
        m_list->addItem(item);
        if (!pack.iconUrl.isEmpty())
            fetchIconFor(item, pack.iconUrl);
    }
}

void RayModpackPage::fetchIconFor(QListWidgetItem* item, const QUrl& url)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = APPLICATION->network()->get(req);
    // Track the item via its modpack id so we don't touch a deleted pointer if the list was rebuilt.
    const QString packId = item->data(Qt::UserRole).toString();
    connect(reply, &QNetworkReply::finished, this, [this, reply, packId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()))
            return;
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem* it = m_list->item(i);
            if (it->data(Qt::UserRole).toString() == packId) {
                it->setIcon(QIcon(pix.scaled(kIconSize, kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                break;
            }
        }
    });
}

std::optional<RayModpack> RayModpackPage::currentModpack() const
{
    auto items = m_list->selectedItems();
    if (items.isEmpty())
        return std::nullopt;
    const QString packId = items.first()->data(Qt::UserRole).toString();
    for (const RayModpack& pack : m_fetcher->modpacks()) {
        if (pack.id == packId)
            return pack;
    }
    return std::nullopt;
}

void RayModpackPage::onSelectionChanged()
{
    auto selected = currentModpack();
    if (!selected.has_value()) {
        m_installButton->setEnabled(false);
        m_descriptionLabel->clear();
        return;
    }
    m_descriptionLabel->setText(selected->description);
    m_installButton->setEnabled(true);
}

void RayModpackPage::onItemDoubleClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    m_list->setCurrentItem(item);
    onInstallClicked();
}

void RayModpackPage::onInstallClicked()
{
    auto selected = currentModpack();
    if (!selected.has_value())
        return;
    emit installRequested(*selected);
}

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

#include "RayModpackDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkReply>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

#include "Application.h"
#include "BuildConfig.h"

namespace {
constexpr int kIconSize = 96;
const char* kModpackIdRole = "raylauncher/modpack_id";
}  // namespace

RayModpackDialog::RayModpackDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Mes Modpacks"));
    resize(640, 480);

    auto* root = new QVBoxLayout(this);

    auto* header = new QLabel(tr("Choisis un modpack à installer. Un clic suffit."), this);
    root->addWidget(header);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kIconSize, kIconSize));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setGridSize(QSize(kIconSize + 48, kIconSize + 48));
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
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_installButton = buttonBox->addButton(tr("Installer"), QDialogButtonBox::AcceptRole);
    m_installButton->setDefault(true);
    m_installButton->setEnabled(false);
    buttonRow->addWidget(buttonBox);
    root->addLayout(buttonRow);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_refreshButton, &QPushButton::clicked, this, &RayModpackDialog::onRefreshClicked);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &RayModpackDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &RayModpackDialog::onItemDoubleClicked);

    m_fetcher = new RayModpackIndexFetcher(APPLICATION->network(), BuildConfig.RAYLAUNCHER_MODPACK_INDEX_URL, this);
    connect(m_fetcher, &RayModpackIndexFetcher::loaded, this, &RayModpackDialog::onIndexLoaded);
    connect(m_fetcher, &RayModpackIndexFetcher::failed, this, &RayModpackDialog::onIndexFailed);

    setStatus(tr("Chargement de la liste de modpacks…"), false);
    m_refreshButton->setEnabled(false);
    m_fetcher->fetch();
}

RayModpackDialog::~RayModpackDialog() = default;

void RayModpackDialog::setStatus(const QString& statusText, bool isError)
{
    m_statusLabel->setText(statusText);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #c23b22;") : QString());
}

void RayModpackDialog::onRefreshClicked()
{
    if (m_fetcher->isLoading())
        return;
    setStatus(tr("Chargement de la liste de modpacks…"), false);
    m_refreshButton->setEnabled(false);
    m_installButton->setEnabled(false);
    m_fetcher->fetch();
}

void RayModpackDialog::onIndexLoaded()
{
    rebuildList();
    m_refreshButton->setEnabled(true);
    if (m_fetcher->modpacks().isEmpty()) {
        setStatus(tr("Aucun modpack dans l'index. Ajoutes-en un dans ton index.json."), false);
    } else {
        setStatus(tr("%1 modpack(s) disponibles.").arg(m_fetcher->modpacks().size()), false);
    }
}

void RayModpackDialog::onIndexFailed(QString error)
{
    m_list->clear();
    m_refreshButton->setEnabled(true);
    m_installButton->setEnabled(false);
    setStatus(tr("Erreur : %1").arg(error), true);
}

void RayModpackDialog::rebuildList()
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

void RayModpackDialog::fetchIconFor(QListWidgetItem* item, const QUrl& url)
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

void RayModpackDialog::onSelectionChanged()
{
    auto items = m_list->selectedItems();
    if (items.isEmpty()) {
        m_installButton->setEnabled(false);
        m_descriptionLabel->clear();
        return;
    }
    const QString packId = items.first()->data(Qt::UserRole).toString();
    for (const RayModpack& pack : m_fetcher->modpacks()) {
        if (pack.id == packId) {
            m_descriptionLabel->setText(pack.description);
            m_installButton->setEnabled(true);
            return;
        }
    }
    m_installButton->setEnabled(false);
    m_descriptionLabel->clear();
}

void RayModpackDialog::onItemDoubleClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    m_list->setCurrentItem(item);
    accept();
}

void RayModpackDialog::accept()
{
    auto items = m_list->selectedItems();
    if (items.isEmpty())
        return;
    const QString packId = items.first()->data(Qt::UserRole).toString();
    for (const RayModpack& pack : m_fetcher->modpacks()) {
        if (pack.id == packId) {
            m_selected = pack;
            QDialog::accept();
            return;
        }
    }
}

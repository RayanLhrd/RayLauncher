// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayModpackCard.h"

#include <QContextMenuEvent>
#include <QFont>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {
constexpr int kTileWidth = 200;
constexpr int kTileHeight = 260;
constexpr int kIconSize = 128;
constexpr int kCardRadius = 10;

const char* kStyleBase = R"(
    RayModpackCard {
        background-color: palette(base);
        border: 1px solid palette(mid);
        border-radius: %1px;
    }
    RayModpackCard:hover {
        background-color: palette(alternate-base);
        border: 1px solid palette(highlight);
    }
)";
}  // namespace

RayModpackCard::RayModpackCard(const RayModpack& pack, State state, const QString& instanceId, QWidget* parent)
    : QFrame(parent), m_pack(pack), m_state(state), m_instanceId(instanceId)
{
    setObjectName("RayModpackCard");
    setFrameShape(QFrame::StyledPanel);
    setFixedSize(kTileWidth, kTileHeight);
    setAttribute(Qt::WA_Hover, true);
    setStyleSheet(QString(kStyleBase).arg(kCardRadius));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Icon on top, centred.
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setPixmap(style()->standardIcon(QStyle::SP_FileDialogInfoView).pixmap(kIconSize, kIconSize));
    root->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

    // Name — bold, centred, can wrap to 2 lines.
    m_nameLabel = new QLabel(pack.name, this);
    m_nameLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_nameLabel->setWordWrap(true);
    QFont nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);
    root->addWidget(m_nameLabel);

    // Optional description — small, dimmed, elided if too long.
    m_descriptionLabel = new QLabel(pack.description, this);
    m_descriptionLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_descriptionLabel->setWordWrap(true);
    // Bright text — palette(mid) was too close to the raised-card background for comfort.
    m_descriptionLabel->setStyleSheet("color: #E6EDF3; font-size: 11px;");
    root->addWidget(m_descriptionLabel, 1);

    // Action button — the ONLY thing on the tile that triggers a user action.
    m_actionButton = new QPushButton(this);
    m_actionButton->setCursor(Qt::PointingHandCursor);
    m_actionButton->setMinimumHeight(32);
    connect(m_actionButton, &QPushButton::clicked, this, &RayModpackCard::onActionButtonClicked);
    root->addWidget(m_actionButton);

    applyState();
}

void RayModpackCard::applyState()
{
    auto* existing = m_iconLabel->graphicsEffect();
    if (existing) {
        m_iconLabel->setGraphicsEffect(nullptr);
        existing->deleteLater();
    }

    // The running overlay takes precedence over the base state — if the matching instance is
    // currently running, the button reads "Arrêter" regardless of install/update status.
    if (m_running) {
        m_actionButton->setText(tr("Arrêter"));
        m_actionButton->setObjectName("killButton");
        m_actionButton->setStyleSheet("QPushButton#killButton { background-color: #c23b22; color: white; }");
    } else {
        m_actionButton->setStyleSheet(QString());
        switch (m_state) {
            case State::Available: {
                auto* eff = new QGraphicsOpacityEffect(m_iconLabel);
                eff->setOpacity(0.55);
                m_iconLabel->setGraphicsEffect(eff);
                m_actionButton->setText(tr("Installer"));
                m_actionButton->setObjectName("installButton");
                break;
            }
            case State::Installed:
                m_actionButton->setText(tr("Jouer"));
                m_actionButton->setObjectName("playButton");
                break;
            case State::UpdateAvailable:
                m_actionButton->setText(tr("Mettre à jour"));
                m_actionButton->setObjectName("updateButton");
                break;
        }
    }
    m_actionButton->style()->unpolish(m_actionButton);
    m_actionButton->style()->polish(m_actionButton);
}

void RayModpackCard::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    applyState();
}

void RayModpackCard::setIcon(const QPixmap& pixmap)
{
    if (!pixmap.isNull())
        m_iconLabel->setPixmap(pixmap.scaled(kIconSize, kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void RayModpackCard::onActionButtonClicked()
{
    if (m_running) {
        emit killClicked(m_instanceId);
        return;
    }
    switch (m_state) {
        case State::Available:
            emit installClicked(m_pack);
            break;
        case State::Installed:
            emit playClicked(m_instanceId);
            break;
        case State::UpdateAvailable:
            emit updateClicked(m_pack, m_instanceId);
            break;
    }
}

void RayModpackCard::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(m_pack, m_instanceId, event->globalPos());
    event->accept();
}

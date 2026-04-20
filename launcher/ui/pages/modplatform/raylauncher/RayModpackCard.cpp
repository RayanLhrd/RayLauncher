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

#include <QFont>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {
constexpr int kIconSize = 96;
constexpr int kCardMinHeight = 120;
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
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(kCardMinHeight);
    setAttribute(Qt::WA_Hover, true);
    setStyleSheet(QString(kStyleBase).arg(kCardRadius));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(14);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setScaledContents(true);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setPixmap(style()->standardIcon(QStyle::SP_FileDialogInfoView).pixmap(kIconSize, kIconSize));
    root->addWidget(m_iconLabel, 0, Qt::AlignTop);

    auto* textColumn = new QVBoxLayout();
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(4);

    m_nameLabel = new QLabel(pack.name, this);
    QFont nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    m_nameLabel->setFont(nameFont);
    textColumn->addWidget(m_nameLabel);

    m_descriptionLabel = new QLabel(pack.description, this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet("color: palette(mid);");
    textColumn->addWidget(m_descriptionLabel);
    textColumn->addStretch(1);

    root->addLayout(textColumn, 1);

    m_actionButton = new QPushButton(this);
    m_actionButton->setCursor(Qt::PointingHandCursor);
    m_actionButton->setMinimumWidth(120);
    m_actionButton->setMinimumHeight(36);
    connect(m_actionButton, &QPushButton::clicked, this, &RayModpackCard::onActionButtonClicked);
    root->addWidget(m_actionButton, 0, Qt::AlignVCenter);

    applyState();
}

void RayModpackCard::applyState()
{
    // Default state: mark Available cards as dimmed by greying out the icon + text via an opacity effect.
    // Installed cards keep full color.
    auto* existing = m_iconLabel->graphicsEffect();
    if (existing) {
        m_iconLabel->setGraphicsEffect(nullptr);
        existing->deleteLater();
    }

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
    // Ensure the button picks up any future stylesheet change keyed on objectName.
    m_actionButton->style()->unpolish(m_actionButton);
    m_actionButton->style()->polish(m_actionButton);
}

void RayModpackCard::setIcon(const QPixmap& pixmap)
{
    if (!pixmap.isNull())
        m_iconLabel->setPixmap(pixmap.scaled(kIconSize, kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void RayModpackCard::onActionButtonClicked()
{
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

void RayModpackCard::mousePressEvent(QMouseEvent* event)
{
    // Entire card is clickable — click anywhere fires the state-appropriate signal,
    // but only for the primary mouse button and only when not landing on the explicit
    // action button (Qt will have already routed the press to it in that case).
    if (event->button() == Qt::LeftButton) {
        onActionButtonClicked();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void RayModpackCard::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    // The :hover pseudo-state in the stylesheet handles the visual; nothing extra needed here for now.
}

void RayModpackCard::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
}

// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RayMemoryDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// Preset values in MB. The labels are computed at runtime so we can show "4 Go" / "6 Go" etc.
constexpr int kPresetsMb[] = { 4096, 6144, 8192, 10240 };
constexpr int kPresetCount = sizeof(kPresetsMb) / sizeof(kPresetsMb[0]);

// Spinbox bounds. 1 GB minimum (less than that won't even start vanilla Minecraft reliably);
// 32 GB ceiling to cover power-user PCs without ridiculous typos.
constexpr int kMinMb = 1024;
constexpr int kMaxMb = 32768;
constexpr int kStepMb = 512;

constexpr const char* kPresetBaseStyle = R"QSS(
    QPushButton#raymem-preset {
        background-color: #1E252F;
        border: 1px solid #2A323F;
        border-radius: 8px;
        padding: 12px 18px;
        color: #E6EDF3;
        font-weight: 600;
        min-width: 70px;
    }
    QPushButton#raymem-preset:hover {
        border-color: #5BC85C;
    }
    QPushButton#raymem-preset:checked {
        background-color: #5BC85C;
        color: #13181F;
        border-color: #5BC85C;
    }
    QPushButton#raymem-preset[recommended="true"] {
        border: 2px solid #5BC85C;
    }
)QSS";
}  // namespace

int RayMemoryDialog::chooseMemory(QWidget* parent, const QString& packDisplayName, int currentMb, int recommendedMb)
{
    RayMemoryDialog dlg(packDisplayName, currentMb, recommendedMb, parent);
    if (dlg.exec() != QDialog::Accepted)
        return -1;
    return dlg.m_chosenMb;
}

RayMemoryDialog::RayMemoryDialog(const QString& packDisplayName, int currentMb, int recommendedMb, QWidget* parent)
    : QDialog(parent), m_recommendedMb(recommendedMb), m_chosenMb(currentMb)
{
    setWindowTitle(packDisplayName.isEmpty() ? tr("Mémoire allouée") : tr("Mémoire allouée — %1").arg(packDisplayName));
    setModal(true);
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(14);

    auto* header = new QLabel(tr("Préréglages"), this);
    QFont headerFont = header->font();
    headerFont.setPointSize(headerFont.pointSize() + 1);
    headerFont.setBold(true);
    header->setFont(headerFont);
    root->addWidget(header);

    // Preset buttons in a horizontal row, with the recommended subtitle slot underneath via a
    // small grid: row 0 = buttons, row 1 = subtitle (only one cell populated).
    auto* presetGrid = new QGridLayout();
    presetGrid->setContentsMargins(0, 0, 0, 0);
    presetGrid->setHorizontalSpacing(8);
    presetGrid->setVerticalSpacing(2);

    setStyleSheet(kPresetBaseStyle);

    for (int i = 0; i < kPresetCount; ++i) {
        const int mb = kPresetsMb[i];
        auto* btn = new QPushButton(tr("%1 Go").arg(mb / 1024), this);
        btn->setObjectName("raymem-preset");
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        if (mb == m_recommendedMb)
            btn->setProperty("recommended", true);
        connect(btn, &QPushButton::clicked, this, [this, mb]() { onPresetClicked(mb); });
        presetGrid->addWidget(btn, 0, i);
        m_presetButtons.append(btn);
        m_presetValuesMb.append(mb);

        if (mb == m_recommendedMb) {
            auto* subtitle = new QLabel(tr("★ Recommandé"), this);
            subtitle->setStyleSheet(QStringLiteral("color: #5BC85C; font-style: italic;"));
            subtitle->setAlignment(Qt::AlignHCenter);
            presetGrid->addWidget(subtitle, 1, i);
            m_recommendedLabel = subtitle;
        }
    }
    root->addLayout(presetGrid);

    // Custom row.
    auto* customRow = new QHBoxLayout();
    auto* customLabel = new QLabel(tr("Personnalisée"), this);
    customRow->addWidget(customLabel);

    m_spinBox = new QSpinBox(this);
    m_spinBox->setRange(kMinMb, kMaxMb);
    m_spinBox->setSingleStep(kStepMb);
    m_spinBox->setSuffix(QStringLiteral(" Mo"));
    m_spinBox->setValue(qBound(kMinMb, currentMb > 0 ? currentMb : (m_recommendedMb > 0 ? m_recommendedMb : 4096), kMaxMb));
    connect(m_spinBox, &QSpinBox::editingFinished, this, &RayMemoryDialog::onSpinboxEdited);
    connect(m_spinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { m_chosenMb = m_spinBox->value(); });
    customRow->addStretch(1);
    customRow->addWidget(m_spinBox);
    root->addLayout(customRow);

    // Buttons.
    auto* buttonBox = new QDialogButtonBox(this);
    auto* cancelBtn = buttonBox->addButton(tr("Annuler"), QDialogButtonBox::RejectRole);
    auto* applyBtn = buttonBox->addButton(tr("Appliquer"), QDialogButtonBox::AcceptRole);
    applyBtn->setDefault(true);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    applyBtn->setCursor(Qt::PointingHandCursor);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttonBox);

    // Initial selection: highlight the preset that matches the current value, otherwise none
    // (the spinbox already shows the right value).
    selectPreset(m_spinBox->value());
}

void RayMemoryDialog::selectPreset(int valueMb)
{
    for (int i = 0; i < m_presetButtons.size(); ++i) {
        m_presetButtons[i]->setChecked(m_presetValuesMb[i] == valueMb);
    }
}

void RayMemoryDialog::onPresetClicked(int valueMb)
{
    m_chosenMb = valueMb;
    m_spinBox->blockSignals(true);
    m_spinBox->setValue(valueMb);
    m_spinBox->blockSignals(false);
    selectPreset(valueMb);
}

void RayMemoryDialog::onSpinboxEdited()
{
    m_chosenMb = m_spinBox->value();
    selectPreset(m_chosenMb);  // clears all if no preset matches
}

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
#include <QSlider>
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
    /* Slider — dark thin groove, accent-coloured fill, big round handle for easy grabbing. */
    QSlider#raymem-slider::groove:horizontal {
        background: #0E1217;
        height: 6px;
        border-radius: 3px;
    }
    QSlider#raymem-slider::sub-page:horizontal {
        background: #5BC85C;
        height: 6px;
        border-radius: 3px;
    }
    QSlider#raymem-slider::handle:horizontal {
        background: #E6EDF3;
        border: 2px solid #5BC85C;
        width: 16px;
        height: 16px;
        margin: -7px 0;
        border-radius: 10px;
    }
    QSlider#raymem-slider::handle:horizontal:hover {
        background: #5BC85C;
    }
    /* The live value label sitting next to the slider — make it big enough that "6144 Mo"
       never gets clipped, regardless of the dialog's chrome. */
    QLabel#raymem-value {
        color: #E6EDF3;
        font-weight: 600;
        font-size: 13px;
        min-width: 90px;
        padding: 4px 8px;
        background-color: #0E1217;
        border: 1px solid #2A323F;
        border-radius: 6px;
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

    // Custom row — slider with a live value label on the right. The label is its own styled
    // pill so the value never gets clipped by an undersized spinbox (which was the issue with
    // the previous QSpinBox layout — "6144 Mo" rendered as "6144 M.").
    auto* customLabelRow = new QHBoxLayout();
    auto* customLabel = new QLabel(tr("Personnalisée"), this);
    QFont customLabelFont = customLabel->font();
    customLabelFont.setBold(true);
    customLabel->setFont(customLabelFont);
    customLabelRow->addWidget(customLabel);
    customLabelRow->addStretch(1);
    root->addLayout(customLabelRow);

    auto* sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(12);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName("raymem-slider");
    m_slider->setRange(kMinMb, kMaxMb);
    m_slider->setSingleStep(kStepMb);
    m_slider->setPageStep(1024);
    m_slider->setTracking(true);
    m_slider->setCursor(Qt::PointingHandCursor);
    const int initialMb = qBound(kMinMb, currentMb > 0 ? currentMb : (m_recommendedMb > 0 ? m_recommendedMb : 4096), kMaxMb);
    m_slider->setValue(initialMb);
    connect(m_slider, &QSlider::valueChanged, this, &RayMemoryDialog::onSliderMoved);
    sliderRow->addWidget(m_slider, 1);

    m_valueLabel = new QLabel(tr("%1 Mo").arg(initialMb), this);
    m_valueLabel->setObjectName("raymem-value");
    m_valueLabel->setAlignment(Qt::AlignCenter);
    sliderRow->addWidget(m_valueLabel);

    root->addLayout(sliderRow);

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
    // (the slider already shows the right value via its handle position).
    selectPreset(m_slider->value());
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
    // Sync the slider visually but block its signal so we don't re-enter selectPreset() with
    // a stale notion of "user just dragged" (which would needlessly clear and re-set the same
    // preset).
    m_slider->blockSignals(true);
    m_slider->setValue(valueMb);
    m_slider->blockSignals(false);
    m_valueLabel->setText(tr("%1 Mo").arg(valueMb));
    selectPreset(valueMb);
}

void RayMemoryDialog::onSliderMoved(int valueMb)
{
    // Snap to step boundaries so the persisted value is always a clean multiple — Qt's slider
    // only enforces step on keyboard nudges, not drag.
    const int snapped = (valueMb / kStepMb) * kStepMb;
    if (snapped != valueMb) {
        m_slider->blockSignals(true);
        m_slider->setValue(snapped);
        m_slider->blockSignals(false);
    }
    m_chosenMb = snapped;
    m_valueLabel->setText(tr("%1 Mo").arg(snapped));
    // Keep the preset row in sync — exact match → that preset checked, anything else → none.
    selectPreset(snapped);
}

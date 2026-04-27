// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QPushButton;
class QSlider;

/**
 * @brief Modal "Mémoire allouée" dialog — preset buttons (4/6/8/10 GB) + free-form spinbox.
 *
 * Two roles drive the visual state:
 *
 *   - **Current**: the value the instance currently has (MaxMemAlloc). The matching preset
 *     button (if any) is shown selected; otherwise the spinbox is the source of truth.
 *   - **Recommended**: what the catalogue author shipped for this pack via
 *     `recommended_memory_mb`. The matching preset gets a green outline + a "★ Recommandé"
 *     subtitle. Survives independently of the current selection — recommendation is a
 *     property of the pack, not of the instance.
 *
 * Static API: open the dialog, get back the user's choice in MB (or -1 on cancel).
 */
class RayMemoryDialog : public QDialog {
    Q_OBJECT
   public:
    /// Open the dialog modally. Returns the chosen value in MB, or -1 if the user cancelled.
    /// @p recommendedMb may be 0 to indicate "no author recommendation" (no star shown).
    static int chooseMemory(QWidget* parent, const QString& packDisplayName, int currentMb, int recommendedMb);

   private:
    RayMemoryDialog(const QString& packDisplayName, int currentMb, int recommendedMb, QWidget* parent);

    void selectPreset(int valueMb);  ///< Visually mark a preset as the active selection.
    void onPresetClicked(int valueMb);
    void onSliderMoved(int valueMb);

    int m_recommendedMb;
    int m_chosenMb;

    QSlider* m_slider = nullptr;
    QLabel* m_valueLabel = nullptr;  ///< Live readout of the slider value, e.g. "6144 Mo".
    QList<QPushButton*> m_presetButtons;
    QList<int> m_presetValuesMb;
    QLabel* m_recommendedLabel = nullptr;  ///< The "★ Recommandé" subtitle, parented under the matching preset.
};

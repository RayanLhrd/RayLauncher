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
#include <QString>

#include "tasks/Task.h"

class QLabel;
class QProgressBar;
class QPushButton;

/**
 * @brief A deliberately-minimal modal dialog shown while a modpack install is in progress.
 *
 * Unlike the default ProgressDialog (which shows many sub-task rows, a detailed log, etc.),
 * this dialog is the bare essentials for an end-user who doesn't care what is being downloaded
 * or from where — just whether the download is still moving:
 *
 *   ┌──────────────────────────────────────┐
 *   │  Installation de Rush Ender Dragon   │
 *   │                                      │
 *   │  ████████░░░░░░░░░░░░░░   37%        │
 *   │                                      │
 *   │  Téléchargement des mods…            │
 *   │                                      │
 *   │                        [ Annuler ]   │
 *   └──────────────────────────────────────┘
 *
 * The status line comes directly from the wrapped Task::status signal. If the task doesn't
 * report a total size, the progress bar goes indeterminate.
 */
class RaySimpleProgressDialog : public QDialog {
    Q_OBJECT
   public:
    explicit RaySimpleProgressDialog(QWidget* parent = nullptr);
    ~RaySimpleProgressDialog() override;

    /// Set the big headline label (e.g. "Installation de <Modpack Name>").
    void setHeadline(const QString& text);

    /// Attach and run a task. The dialog blocks until the task finishes (success, failure, abort).
    /// Returns QDialog::Accepted on success, QDialog::Rejected otherwise.
    int execWithTask(Task* task);

   protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

   private slots:
    void onTaskProgress(qint64 current, qint64 total);
    void onTaskStatus(const QString& status);
    void onTaskSucceeded();
    void onTaskFailed(const QString& reason);
    void onTaskAborted();
    void onAbortClicked();

   private:
    QLabel* m_headlineLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_percentLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_abortButton = nullptr;

    Task* m_task = nullptr;
    bool m_finished = false;
};

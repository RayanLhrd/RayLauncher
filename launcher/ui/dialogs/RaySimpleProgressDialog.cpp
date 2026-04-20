// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RaySimpleProgressDialog.h"

#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

RaySimpleProgressDialog::RaySimpleProgressDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Installation…"));
    setModal(true);
    setSizeGripEnabled(false);
    setMinimumWidth(440);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(14);

    m_headlineLabel = new QLabel(tr("Installation en cours…"), this);
    QFont headlineFont = m_headlineLabel->font();
    headlineFont.setBold(true);
    headlineFont.setPointSize(headlineFont.pointSize() + 1);
    m_headlineLabel->setFont(headlineFont);
    m_headlineLabel->setWordWrap(true);
    root->addWidget(m_headlineLabel);

    auto* progressRow = new QHBoxLayout();
    progressRow->setSpacing(10);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);  // indeterminate until we get a first progress event
    m_progressBar->setTextVisible(false);
    m_progressBar->setMinimumHeight(18);
    progressRow->addWidget(m_progressBar, 1);

    m_percentLabel = new QLabel(this);
    m_percentLabel->setMinimumWidth(44);
    m_percentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(m_percentLabel, 0);

    root->addLayout(progressRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: palette(mid);");
    root->addWidget(m_statusLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    m_abortButton = new QPushButton(tr("Annuler"), this);
    m_abortButton->setCursor(Qt::PointingHandCursor);
    connect(m_abortButton, &QPushButton::clicked, this, &RaySimpleProgressDialog::onAbortClicked);
    buttonRow->addWidget(m_abortButton);
    root->addLayout(buttonRow);
}

RaySimpleProgressDialog::~RaySimpleProgressDialog() = default;

void RaySimpleProgressDialog::setHeadline(const QString& text)
{
    m_headlineLabel->setText(text);
}

int RaySimpleProgressDialog::execWithTask(Task* task)
{
    m_task = task;
    m_finished = false;

    connect(task, &Task::progress, this, &RaySimpleProgressDialog::onTaskProgress);
    connect(task, &Task::status, this, &RaySimpleProgressDialog::onTaskStatus);
    connect(task, &Task::succeeded, this, &RaySimpleProgressDialog::onTaskSucceeded);
    connect(task, &Task::failed, this, &RaySimpleProgressDialog::onTaskFailed);
    connect(task, &Task::aborted, this, &RaySimpleProgressDialog::onTaskAborted);

    if (!task->isRunning())
        task->start();

    return QDialog::exec();
}

void RaySimpleProgressDialog::onTaskProgress(qint64 current, qint64 total)
{
    if (total > 0) {
        m_progressBar->setRange(0, 100);
        const int pct = static_cast<int>((100 * current) / total);
        m_progressBar->setValue(pct);
        m_percentLabel->setText(QStringLiteral("%1%").arg(pct));
    } else {
        m_progressBar->setRange(0, 0);  // indeterminate
        m_percentLabel->setText(QString());
    }
}

void RaySimpleProgressDialog::onTaskStatus(const QString& status)
{
    m_statusLabel->setText(status);
}

void RaySimpleProgressDialog::onTaskSucceeded()
{
    m_finished = true;
    QDialog::accept();
}

void RaySimpleProgressDialog::onTaskFailed(const QString& /*reason*/)
{
    // The caller owns the error presentation — the install flow shows a CustomMessageBox on failure.
    m_finished = true;
    QDialog::reject();
}

void RaySimpleProgressDialog::onTaskAborted()
{
    m_finished = true;
    QDialog::reject();
}

void RaySimpleProgressDialog::onAbortClicked()
{
    if (m_task && m_task->isRunning()) {
        m_abortButton->setEnabled(false);
        m_statusLabel->setText(tr("Annulation en cours…"));
        m_task->abort();
        return;
    }
    QDialog::reject();
}

void RaySimpleProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_finished && m_task && m_task->isRunning()) {
        // Treat the close button as Abort to avoid orphan tasks.
        m_task->abort();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void RaySimpleProgressDialog::keyPressEvent(QKeyEvent* event)
{
    // Block Escape from cancelling the dialog silently — make the user confirm via the Annuler button.
    if (event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

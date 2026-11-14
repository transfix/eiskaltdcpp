/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#include "SettingsMedia.h"
#include "QtContextAware.h"
#include "QtContext.h"

#include "WulforSettings.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

#ifndef NO_QT_MULTIMEDIA
#include <QMediaDevices>
#include <QCameraDevice>
#include <QAudioDevice>
#endif

SettingsMedia::SettingsMedia(QWidget *parent) :
    QWidget(parent)
{
    buildUi();
    populateDevices();
    loadSettings();
}

SettingsMedia::~SettingsMedia()
{
    stopPreview();
}

void SettingsMedia::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // --- Devices ---
    auto *devBox = new QGroupBox(tr("Devices"), this);
    auto *devForm = new QFormLayout(devBox);

    cameraCombo_ = new QComboBox(devBox);
    micCombo_    = new QComboBox(devBox);
    devForm->addRow(tr("Camera:"), cameraCombo_);
    devForm->addRow(tr("Microphone:"), micCombo_);
    root->addWidget(devBox);

    // --- Default call state ---
    auto *defBox = new QGroupBox(tr("Call defaults"), this);
    auto *defLay = new QVBoxLayout(defBox);
    micMuted_  = new QCheckBox(tr("Start calls with the microphone muted"), defBox);
    cameraOff_ = new QCheckBox(tr("Start calls with the camera turned off"), defBox);
    defLay->addWidget(micMuted_);
    defLay->addWidget(cameraOff_);
    root->addWidget(defBox);

    // --- Camera preview ---
    auto *prevBox = new QGroupBox(tr("Camera preview"), this);
    auto *prevLay = new QVBoxLayout(prevBox);

    previewBtn_ = new QPushButton(tr("Start preview"), prevBox);
    previewBtn_->setCheckable(true);
    prevLay->addWidget(previewBtn_);

#if !defined(NO_QT_MULTIMEDIA) && !defined(NO_QT_MULTIMEDIAWIDGETS)
    videoWidget_ = new QVideoWidget(prevBox);
    videoWidget_->setMinimumHeight(180);
    prevLay->addWidget(videoWidget_);
    connect(previewBtn_, &QPushButton::toggled, this, &SettingsMedia::slotTogglePreview);
    connect(cameraCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsMedia::slotCameraChanged);
#else
    previewInfo_ = new QLabel(prevBox);
    previewInfo_->setWordWrap(true);
    previewInfo_->setText(tr("Live camera preview is unavailable: this build was "
                             "compiled without Qt MultimediaWidgets."));
    prevLay->addWidget(previewInfo_);
    previewBtn_->setEnabled(false);
#endif

    root->addWidget(prevBox);
    root->addStretch(1);
}

void SettingsMedia::populateDevices()
{
    // Index 0 is always "system default" (empty device id).
    cameraCombo_->addItem(tr("System default"), QString());
    micCombo_->addItem(tr("System default"), QString());

#ifndef NO_QT_MULTIMEDIA
    for (const QCameraDevice &dev : QMediaDevices::videoInputs())
        cameraCombo_->addItem(dev.description(), QString::fromUtf8(dev.id()));
    for (const QAudioDevice &dev : QMediaDevices::audioInputs())
        micCombo_->addItem(dev.description(), QString::fromUtf8(dev.id()));
#else
    cameraCombo_->setEnabled(false);
    micCombo_->setEnabled(false);
#endif
}

void SettingsMedia::loadSettings()
{
    const QString cam = qtCtx()->settings()->getStr(WS_MEDIA_CAMERA_DEVICE);
    const QString mic = qtCtx()->settings()->getStr(WS_MEDIA_MIC_DEVICE);

    int ci = cameraCombo_->findData(cam);
    cameraCombo_->setCurrentIndex(ci < 0 ? 0 : ci);
    int mi = micCombo_->findData(mic);
    micCombo_->setCurrentIndex(mi < 0 ? 0 : mi);

    micMuted_->setChecked(qtCtx()->settings()->getBool(WB_MEDIA_MIC_MUTED_DEFAULT));
    cameraOff_->setChecked(qtCtx()->settings()->getBool(WB_MEDIA_CAMERA_OFF_DEFAULT));
}

void SettingsMedia::ok()
{
    qtCtx()->settings()->setStr(WS_MEDIA_CAMERA_DEVICE, cameraCombo_->currentData().toString());
    qtCtx()->settings()->setStr(WS_MEDIA_MIC_DEVICE, micCombo_->currentData().toString());
    qtCtx()->settings()->setBool(WB_MEDIA_MIC_MUTED_DEFAULT, micMuted_->isChecked());
    qtCtx()->settings()->setBool(WB_MEDIA_CAMERA_OFF_DEFAULT, cameraOff_->isChecked());
    stopPreview();
}

void SettingsMedia::slotTogglePreview(bool on)
{
    if (on) {
        previewBtn_->setText(tr("Stop preview"));
        startPreview();
    } else {
        previewBtn_->setText(tr("Start preview"));
        stopPreview();
    }
}

void SettingsMedia::slotCameraChanged(int)
{
    // Restart the preview on the newly selected device if it is running.
    if (previewBtn_ && previewBtn_->isChecked()) {
        stopPreview();
        startPreview();
    }
}

void SettingsMedia::startPreview()
{
#if !defined(NO_QT_MULTIMEDIA) && !defined(NO_QT_MULTIMEDIAWIDGETS)
    const QString id = cameraCombo_->currentData().toString();
    QCameraDevice chosen; // default-constructed → system default
    for (const QCameraDevice &dev : QMediaDevices::videoInputs()) {
        if (QString::fromUtf8(dev.id()) == id) {
            chosen = dev;
            break;
        }
    }
    if (chosen.isNull())
        chosen = QMediaDevices::defaultVideoInput();
    if (chosen.isNull())
        return; // no camera available

    if (!captureSession_)
        captureSession_ = new QMediaCaptureSession(this);
    if (camera_) {
        camera_->stop();
        camera_->deleteLater();
    }
    camera_ = new QCamera(chosen, this);
    captureSession_->setCamera(camera_);
    captureSession_->setVideoOutput(videoWidget_);
    camera_->start();
#endif
}

void SettingsMedia::stopPreview()
{
#ifndef NO_QT_MULTIMEDIA
    if (camera_) {
        camera_->stop();
        camera_->deleteLater();
        camera_ = nullptr;
    }
#endif
}

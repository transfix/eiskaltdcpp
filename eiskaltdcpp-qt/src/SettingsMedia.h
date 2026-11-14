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

#pragma once

#include <QWidget>

#ifndef NO_QT_MULTIMEDIA
#include <QMediaCaptureSession>
#include <QCamera>
#endif
#if !defined(NO_QT_MULTIMEDIA) && !defined(NO_QT_MULTIMEDIAWIDGETS)
#include <QVideoWidget>
#endif

class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;

/// Media preferences page: pick the camera and microphone used for NMDCpb
/// voice/video calls, choose the default in-call mute state, and (when Qt
/// Multimedia + MultimediaWidgets are available) show a live camera preview.
///
/// Follows the same contract as the other Settings* pages: constructed by
/// Settings::init(), state is loaded in the ctor and written back in ok()
/// (connected to Settings::timeToDie). Device identity is persisted by the
/// QMediaDevices device id (stable string) in WS_MEDIA_CAMERA_DEVICE /
/// WS_MEDIA_MIC_DEVICE; an empty value means "system default".
class SettingsMedia : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsMedia(QWidget *parent = nullptr);
    ~SettingsMedia() override;

public slots:
    void ok();

private slots:
    void slotTogglePreview(bool on);
    void slotCameraChanged(int index);

private:
    void buildUi();
    void populateDevices();
    void loadSettings();
    void startPreview();
    void stopPreview();

    QComboBox   *cameraCombo_ = nullptr;
    QComboBox   *micCombo_    = nullptr;
    QCheckBox   *micMuted_    = nullptr;
    QCheckBox   *cameraOff_   = nullptr;
    QPushButton *previewBtn_  = nullptr;
    QLabel      *previewInfo_ = nullptr;

#ifndef NO_QT_MULTIMEDIA
    QMediaCaptureSession *captureSession_ = nullptr;
    QCamera              *camera_         = nullptr;
#endif
#if !defined(NO_QT_MULTIMEDIA) && !defined(NO_QT_MULTIMEDIAWIDGETS)
    QVideoWidget *videoWidget_ = nullptr;
#endif
};

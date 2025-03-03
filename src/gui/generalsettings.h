/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef MIRALL_GENERALSETTINGS_H
#define MIRALL_GENERALSETTINGS_H

#include <QWidget>
#include <QPointer>

namespace OCC {
class IgnoreListEditor;
class ScheduleSettings;  
class SyncLogDialog;

namespace Ui {
    class GeneralSettings;
}

/**
 * @brief The GeneralSettings class
 * @ingroup gui
 */
class GeneralSettings : public QWidget
{
    Q_OBJECT

public:
    explicit GeneralSettings(QTimer *scheduleTimer, QWidget *parent = nullptr);
    ~GeneralSettings();
    QSize sizeHint() const override;

private slots:
    void saveMiscSettings();
    void slotToggleLaunchOnStartup(bool);
    void slotToggleOptionalServerNotifications(bool);
    void slotShowInExplorerNavigationPane(bool);
    void slotUpdateInfo();
    void slotIgnoreFilesEditor();
    void slotScheduleSyncing();  
    void loadMiscSettings();
    void slotShowLegalNotice();

private:
    Ui::GeneralSettings *_ui;
    QPointer<IgnoreListEditor> _ignoreEditor;
    QPointer<SyncLogDialog> _syncLogDialog;
    bool _currentlyLoading;
    QTimer *_scheduleTimer;
};


} // namespace OCC
#endif // MIRALL_GENERALSETTINGS_H

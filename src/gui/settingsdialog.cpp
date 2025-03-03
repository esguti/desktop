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

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "folderman.h"
#include "theme.h"
#include "generalsettings.h"
#include "networksettings.h"
#include "accountsettings.h"
#include "configfile.h"
#include "progressdispatcher.h"
#include "owncloudgui.h"
#include "activitywidget.h"
#include "accountmanager.h"
#include "schedulesettings.h"

#include <QLabel>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QPushButton>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QImage>
#include <QWidgetAction>
#include <QPainter>
#include <QPainterPath>

namespace {
const char TOOLBAR_CSS[] =
    "QToolBar { background: %1; margin: 0; padding: 0; border: none; border-bottom: 0 solid %2; spacing: 0; } "
    "QToolBar QToolButton { background: %1; border: none; border-bottom: 0 solid %2; margin: 0; padding: 5px; } "
    "QToolBar QToolBarExtension { padding:0; } "
    "QToolBar QToolButton:checked { background: %3; color: %4; }";

static const float buttonSizeRatio = 1.618f; // golden ratio
}


namespace OCC {

  const char propertyAccountC[] = "oc_account";
  Q_LOGGING_CATEGORY(lcSettings, "nextcloud.gui.settings", QtDebugMsg)
  
#include "settingsdialogcommon.cpp"

//
// Whenever you change something here check both settingsdialog.cpp and settingsdialogmac.cpp !
//

SettingsDialog::SettingsDialog(ownCloudGui *gui, QWidget *parent)
    : QDialog(parent)
    , _ui(new Ui::SettingsDialog)
    , _gui(gui)
{
    ConfigFile cfg;

    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    _ui->setupUi(this);
    _toolBar = new QToolBar;
    _toolBar->setIconSize(QSize(32, 32));
    _toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    layout()->setMenuBar(_toolBar);

    // People perceive this as a Window, so also make Ctrl+W work
    QAction *closeWindowAction = new QAction(this);
    closeWindowAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(closeWindowAction, &QAction::triggered, this, &SettingsDialog::accept);
    addAction(closeWindowAction);

    setObjectName("Settings"); // required as group for saveGeometry call
    setWindowTitle(Theme::instance()->appNameGUI());

    connect(AccountManager::instance(), &AccountManager::accountAdded,
        this, &SettingsDialog::accountAdded);
    connect(AccountManager::instance(), &AccountManager::accountRemoved,
        this, &SettingsDialog::accountRemoved);


    _actionGroup = new QActionGroup(this);
    _actionGroup->setExclusive(true);
    connect(_actionGroup, &QActionGroup::triggered, this, &SettingsDialog::slotSwitchPage);

    _actionBefore = new QAction(this);
    _toolBar->addAction(_actionBefore);

    // Adds space between users + activities and general + schedule + network actions
    QWidget* spacer = new QWidget();
    spacer->setMinimumWidth(10);
    spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
    _toolBar->addWidget(spacer);

    // create timer to check configuration every 5 seconds
    _scheduleTimer = new QTimer(this);
    connect(_scheduleTimer, &QTimer::timeout,
        this, &SettingsDialog::checkSchedule);
    ConfigFile cfgFile;
    if( cfgFile.getScheduleStatus() ){
      _scheduleTimer->start(ScheduleSettings::SCHEDULE_TIME);
    }
    
    QAction *generalAction = createColorAwareAction(QLatin1String(":/client/resources/settings.png"), tr("General"));
    _actionGroup->addAction(generalAction);
    _toolBar->addAction(generalAction);
    GeneralSettings *generalSettings = new GeneralSettings(_scheduleTimer);
    _ui->stack->addWidget(generalSettings);

    QAction *networkAction = createColorAwareAction(QLatin1String(":/client/resources/network.png"), tr("Network"));
    _actionGroup->addAction(networkAction);
    _toolBar->addAction(networkAction);
    NetworkSettings *networkSettings = new NetworkSettings;
    _ui->stack->addWidget(networkSettings);

    _actionGroupWidgets.insert(generalAction, generalSettings);
    _actionGroupWidgets.insert(networkAction, networkSettings);

    foreach(auto ai, AccountManager::instance()->accounts()) {
        accountAdded(ai.data());
    }

    QTimer::singleShot(1, this, &SettingsDialog::showFirstPage);

    QAction *showLogWindow = new QAction(this);
    showLogWindow->setShortcut(QKeySequence("F12"));
    connect(showLogWindow, &QAction::triggered, gui, &ownCloudGui::slotToggleLogBrowser);
    addAction(showLogWindow);

    customizeStyle();

    cfg.restoreGeometry(this);
}

SettingsDialog::~SettingsDialog()
{
    delete _scheduleTimer;
    delete _ui;
}

// close event is not being called here
void SettingsDialog::reject()
{
    ConfigFile cfg;
    cfg.saveGeometry(this);
    QDialog::reject();
}

void SettingsDialog::accept()
{
    ConfigFile cfg;
    cfg.saveGeometry(this);
    QDialog::accept();
}

void SettingsDialog::changeEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ThemeChange:
        customizeStyle();
        break;
    default:
        break;
    }

    QDialog::changeEvent(e);
}

void SettingsDialog::slotSwitchPage(QAction *action)
{
    _ui->stack->setCurrentWidget(_actionGroupWidgets.value(action));
}

void SettingsDialog::showFirstPage()
{
    QList<QAction *> actions = _toolBar->actions();
    if (!actions.empty()) {
        actions.first()->trigger();
    }
}

void SettingsDialog::showActivityPage()
{
    if (auto account = qvariant_cast<AccountState*>(sender()->property("account"))) {
        _activitySettings[account]->show();
        _ui->stack->setCurrentWidget(_activitySettings[account]);
    }
}

void SettingsDialog::showIssuesList(AccountState *account) {
    for (auto it = _actionGroupWidgets.begin(); it != _actionGroupWidgets.end(); ++it) {
        if (it.value() == _activitySettings[account]) {
            it.key()->activate(QAction::ActionEvent::Trigger);
            break;
        }
    }
}

void SettingsDialog::activityAdded(AccountState *s){
    _ui->stack->addWidget(_activitySettings[s]);
    connect(_activitySettings[s], &ActivitySettings::guiLog, _gui,
        &ownCloudGui::slotShowOptionalTrayMessage);

    ConfigFile cfg;
    _activitySettings[s]->setNotificationRefreshInterval(cfg.notificationRefreshInterval());

    // Note: all the actions have a '\n' because the account name is in two lines and
    // all buttons must have the same size in order to keep a good layout
    QAction *action = createColorAwareAction(QLatin1String(":/client/resources/activity.png"), tr("Activity"));
    action->setProperty("account", QVariant::fromValue(s));
    _toolBar->insertAction(_actionBefore, action);
    _actionGroup->addAction(action);
    _actionGroupWidgets.insert(action, _activitySettings[s]);
    connect(action, &QAction::triggered, this, &SettingsDialog::showActivityPage);
}

void SettingsDialog::accountAdded(AccountState *s)
{
    auto height = _toolBar->sizeHint().height();
    bool brandingSingleAccount = !Theme::instance()->multiAccount();

    _activitySettings[s] = new ActivitySettings(s, this);

    // if this is not the first account, then before we continue to add more accounts we add a separator
    if(AccountManager::instance()->accounts().first().data() != s &&
        AccountManager::instance()->accounts().size() >= 1){
        _actionGroupWidgets.insert(_toolBar->insertSeparator(_actionBefore), _activitySettings[s]);
    }

    QAction *accountAction;
    QImage avatar = s->account()->avatar();
    const QString actionText = brandingSingleAccount ? tr("Account") : s->account()->displayName();
    if (avatar.isNull()) {
        accountAction = createColorAwareAction(QLatin1String(":/client/resources/account.png"),
            actionText);
    } else {
        QIcon icon(QPixmap::fromImage(AvatarJob::makeCircularAvatar(avatar)));
        accountAction = createActionWithIcon(icon, actionText);
    }

    if (!brandingSingleAccount) {
        accountAction->setToolTip(s->account()->displayName());
        accountAction->setIconText(SettingsDialogCommon::shortDisplayNameForSettings(s->account().data(),  height * buttonSizeRatio));
    }

    _toolBar->insertAction(_actionBefore, accountAction);
    auto accountSettings = new AccountSettings(s, this);
    _ui->stack->insertWidget(0, accountSettings);
    _actionGroup->addAction(accountAction);
    _actionGroupWidgets.insert(accountAction, accountSettings);
    _actionForAccount.insert(s->account().data(), accountAction);
    accountAction->trigger();

    connect(accountSettings, &AccountSettings::folderChanged, _gui, &ownCloudGui::slotFoldersChanged);
    connect(accountSettings, &AccountSettings::openFolderAlias,
        _gui, &ownCloudGui::slotFolderOpenAction);
    connect(accountSettings, &AccountSettings::showIssuesList, this, &SettingsDialog::showIssuesList);
    connect(s->account().data(), &Account::accountChangedAvatar, this, &SettingsDialog::slotAccountAvatarChanged);
    connect(s->account().data(), &Account::accountChangedDisplayName, this, &SettingsDialog::slotAccountDisplayNameChanged);

    // Refresh immediatly when getting online
    connect(s, &AccountState::isConnectedChanged, this, &SettingsDialog::slotRefreshActivityAccountStateSender);

    activityAdded(s);
    slotRefreshActivity(s);
}

void SettingsDialog::slotAccountAvatarChanged()
{
    Account *account = static_cast<Account *>(sender());
    if (account && _actionForAccount.contains(account)) {
        QAction *action = _actionForAccount[account];
        if (action) {
            QImage pix = account->avatar();
            if (!pix.isNull()) {
                action->setIcon(QPixmap::fromImage(AvatarJob::makeCircularAvatar(pix)));
            }
        }
    }
}

void SettingsDialog::slotAccountDisplayNameChanged()
{
    Account *account = static_cast<Account *>(sender());
    if (account && _actionForAccount.contains(account)) {
        QAction *action = _actionForAccount[account];
        if (action) {
            QString displayName = account->displayName();
            action->setText(displayName);
            auto height = _toolBar->sizeHint().height();
            action->setIconText(SettingsDialogCommon::shortDisplayNameForSettings(account, height * buttonSizeRatio));
        }
    }
}

void SettingsDialog::accountRemoved(AccountState *s)
{
    for (auto it = _actionGroupWidgets.begin(); it != _actionGroupWidgets.end(); ++it) {
        auto as = qobject_cast<AccountSettings *>(*it);
        if (!as) {
            continue;
        }
        if (as->accountsState() == s) {
            _toolBar->removeAction(it.key());

            if (_ui->stack->currentWidget() == it.value()) {
                showFirstPage();
            }

            it.key()->deleteLater();
            it.value()->deleteLater();
            _actionGroupWidgets.erase(it);
            break;
        }
    }

    if (_actionForAccount.contains(s->account().data())) {
        _actionForAccount.remove(s->account().data());
    }

    if(_activitySettings.contains(s)){
        _activitySettings[s]->slotRemoveAccount();
        _activitySettings[s]->hide();

        // get the settings widget and the separator
        foreach(QAction *action, _actionGroupWidgets.keys(_activitySettings[s])){
            _actionGroupWidgets.remove(action);
            _toolBar->removeAction(action);
        }
        _toolBar->widgetForAction(_actionBefore)->hide();
        _activitySettings.remove(s);
    }

    // Hide when the last account is deleted. We want to enter the same
    // state we'd be in the client was started up without an account
    // configured.
    if (AccountManager::instance()->accounts().isEmpty()) {
        hide();
    }
}

void SettingsDialog::customizeStyle()
{
    QString highlightColor(palette().highlight().color().name());
    QString altBase(palette().alternateBase().color().name());
    QString dark(palette().dark().color().name());
    QString background(palette().base().color().name());
    _toolBar->setStyleSheet(QString::fromLatin1(TOOLBAR_CSS).arg(background, dark, highlightColor, altBase));

    Q_FOREACH (QAction *a, _actionGroup->actions()) {
        QIcon icon = createColorAwareIcon(a->property("iconPath").toString());
        a->setIcon(icon);
        QToolButton *btn = qobject_cast<QToolButton *>(_toolBar->widgetForAction(a));
        if (btn)
            btn->setIcon(icon);
    }
}

static bool isDarkColor(const QColor &color)
{
    // account for different sensitivity of the human eye to certain colors
    double treshold = 1.0 - (0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue()) / 255.0;
    return treshold > 0.5;
}

QIcon SettingsDialog::createColorAwareIcon(const QString &name)
{
    QPalette pal = palette();
    QImage img(name);
    QImage inverted(img);
    inverted.invertPixels(QImage::InvertRgb);

    QIcon icon;
    if (isDarkColor(pal.color(QPalette::Base))) {
        icon.addPixmap(QPixmap::fromImage(inverted));
    } else {
        icon.addPixmap(QPixmap::fromImage(img));
    }
    if (isDarkColor(pal.color(QPalette::HighlightedText))) {
        icon.addPixmap(QPixmap::fromImage(img), QIcon::Normal, QIcon::On);
    } else {
        icon.addPixmap(QPixmap::fromImage(inverted), QIcon::Normal, QIcon::On);
    }
    return icon;
}

class ToolButtonAction : public QWidgetAction
{
public:
    explicit ToolButtonAction(const QIcon &icon, const QString &text, QObject *parent)
        : QWidgetAction(parent)
    {
        setText(text);
        setIcon(icon);
    }


    QWidget *createWidget(QWidget *parent) override
    {
        auto toolbar = qobject_cast<QToolBar *>(parent);
        if (!toolbar) {
            // this means we are in the extention menu, no special action here
            return nullptr;
        }

        QToolButton *btn = new QToolButton(parent);
        btn->setDefaultAction(this);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        return btn;
    }
};

QAction *SettingsDialog::createActionWithIcon(const QIcon &icon, const QString &text, const QString &iconPath)
{
    QAction *action = new ToolButtonAction(icon, text, this);
    action->setCheckable(true);
    if (!iconPath.isEmpty()) {
        action->setProperty("iconPath", iconPath);
    }
    return action;
}

QAction *SettingsDialog::createColorAwareAction(const QString &iconPath, const QString &text)
{
    // all buttons must have the same size in order to keep a good layout
    QIcon coloredIcon = createColorAwareIcon(iconPath);
    return createActionWithIcon(coloredIcon, text, iconPath);
}

void SettingsDialog::slotRefreshActivityAccountStateSender()
{
    slotRefreshActivity(qobject_cast<AccountState*>(sender()));
}

void SettingsDialog::slotRefreshActivity(AccountState *accountState)
{
    if (accountState->isConnected())
        _activitySettings[accountState]->slotRefresh();
}

void SettingsDialog::checkSchedule(){
    ConfigFile cfgFile;
    bool timer_table[7][24];
    cfgFile.getScheduleTable(timer_table);

    //activate/deactivate sync depending the day of the week and the hour
    QDate date = QDate::currentDate();
    int day = date.dayOfWeek() - 1;
    QTime time = QTime::currentTime();
    int hour = time.hour();
    if( timer_table[day][hour] ){
        qCDebug(lcSettings) << "Start sync: " << day << " - " << hour;
        this->setPauseOnAllFoldersHelper(false);
    }else{
        qCDebug(lcSettings) << "Stop Sync: " << day << " - " << hour;
        this->setPauseOnAllFoldersHelper(true);
    }
}


void SettingsDialog::setPauseOnAllFoldersHelper(bool pause)
{
    // this funcion is a copy of ownCloudGui::setPauseOnAllFoldersHelper(bool pause)
    QList<AccountState *> accounts;
    if (auto account = qvariant_cast<AccountStatePtr>(sender()->property(propertyAccountC))) {
      accounts.append(account.data());
    } else {
      foreach (auto a, AccountManager::instance()->accounts()) {
        accounts.append(a.data());
      }
    }
    foreach (Folder *f, FolderMan::instance()->map()) {
      if (accounts.contains(f->accountState())) {
        f->setSyncPaused(pause);
        if (pause) {
          f->slotTerminateSync();
        }
      }
    }
}


} // namespace OCC

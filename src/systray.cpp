#include "systray.h"

#include <ksystemtray.h>
#include <tdepopupmenu.h>
#include <tdelocale.h>
#include <tdeapplication.h>
#include <kiconloader.h>
#include <ntqpixmap.h>
#include <ntqimage.h>
#include <ntqiconset.h>
#include <ntqtooltip.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "nodes.h"
#include "embedded_icons.h"
#include "icon_theme.h"

SysTray::SysTray(TQObject* parent, const char* name)
    : TQObject(parent, name),
      m_fwEnabled(false),
      m_connected(false)
{
    setupIcons();

    m_tray = new OpenSnitchTray(0, "opensnitch-systray");
    m_tray->setPixmap(m_iconOff);
    TQToolTip::add(m_tray, "OpenSnitch");
    connect(m_tray, SIGNAL(leftClicked()), this, SLOT(onTrayClicked()));

    // Use the custom menu (no TDE-injected Quit)
    m_menu = m_tray->customMenu;

    m_menuIdMain = m_menu->insertItem(i18n("Open main window"), this, TQT_SLOT(onShowMain()));
    m_menuIdFwToggle = m_menu->insertItem(i18n("Disable"), this, TQT_SLOT(onDisableInterception()));
    m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconPause, TQIconSet::Small), i18n("Disable"));
    m_menu->setItemEnabled(m_menuIdFwToggle, false);
    m_menu->insertSeparator();
    m_menuIdHelp = m_menu->insertItem(i18n("Help"), this, TQT_SLOT(onHelp()));
    m_menu->changeItem(m_menuIdHelp, TQIconSet(m_menuIconHelp, TQIconSet::Small), i18n("Help"));
    m_menuIdQuit = m_menu->insertItem(i18n("Quit"), this, TQT_SLOT(onQuit()));
    m_menu->changeItem(m_menuIdQuit, TQIconSet(m_menuIconQuit, TQIconSet::Small), i18n("Quit"));
}

SysTray::~SysTray()
{
    delete m_tray;
}

void SysTray::show()
{
    m_tray->show();
}

void SysTray::setupIcons()
{
    // Icons are embedded in the binary via xxd (see icons/ directory)
    m_iconOff = IconTheme::loadEmbeddedPixmap(icon_off_png, (int)icon_off_png_len, 0);
    m_iconWhite = IconTheme::loadEmbeddedPixmap(icon_white_png, (int)icon_white_png_len, 0);
    m_iconPause = IconTheme::loadEmbeddedPixmap(icon_pause_png, (int)icon_pause_png_len, 0);
    m_iconAlert = IconTheme::loadEmbeddedPixmap(icon_alert_png, (int)icon_alert_png_len, 0);

    m_menuIconStart = IconTheme::loadEmbeddedPixmap(media_playback_start_png, (int)media_playback_start_png_len, 16);
    m_menuIconPause = IconTheme::loadEmbeddedPixmap(media_playback_pause_png, (int)media_playback_pause_png_len, 16);
    m_menuIconHelp = IconTheme::loadEmbeddedPixmap(quickhelp_png, (int)quickhelp_png_len, 16);
    m_menuIconQuit = IconTheme::loadEmbeddedPixmap(quit_png, (int)quit_png_len, 16);
}

void SysTray::reloadIcons()
{
    setupIcons();

    if (m_menu) {
        m_menu->changeItem(m_menuIdHelp, TQIconSet(m_menuIconHelp, TQIconSet::Small), i18n("Help"));
        m_menu->changeItem(m_menuIdQuit, TQIconSet(m_menuIconQuit, TQIconSet::Small), i18n("Quit"));
        if (m_fwEnabled)
            m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconPause, TQIconSet::Small), i18n("Disable"));
        else
            m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconStart, TQIconSet::Small), i18n("Enable"));
    }

    updateIcon();
}

void SysTray::updateIcon()
{
    if (!m_connected) {
        m_tray->setPixmap(m_iconOff);
    } else if (m_fwEnabled) {
        m_tray->setPixmap(m_iconWhite);
    } else {
        m_tray->setPixmap(m_iconPause);
    }
}

void SysTray::setIconConnected()
{
    m_connected = true;
    updateIcon();
}

void SysTray::setIconDisconnected()
{
    m_connected = false;
    m_fwEnabled = false;
    updateIcon();
}

void SysTray::setIconPaused()
{
    m_tray->setPixmap(m_iconPause);
}

void SysTray::setIconAlert()
{
    m_tray->setPixmap(m_iconAlert);
}

void SysTray::restoreIcon()
{
    updateIcon();
}

void SysTray::setFirewallEnabled(bool enabled)
{
    m_fwEnabled = enabled;
    if (enabled) {
        m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconPause, TQIconSet::Small), i18n("Disable"));
    } else {
        m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconStart, TQIconSet::Small), i18n("Enable"));
    }
    m_menu->setItemEnabled(m_menuIdFwToggle, m_connected);
    updateIcon();
}

void SysTray::setDaemonConnected(bool connected)
{
    m_connected = connected;
    m_menu->setItemEnabled(m_menuIdFwToggle, connected && Nodes::instance()->count() <= 1);
    updateIcon();
}

void SysTray::onDaemonSubscribed(const TQString& peer, bool firewallRunning)
{
    // Matches Python: _set_daemon_connected + _update_fw_status
    m_connected = true;
    m_fwEnabled = firewallRunning;

    // Update menu
    if (m_fwEnabled) {
        m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconPause, TQIconSet::Small), i18n("Disable"));
    } else {
        m_menu->changeItem(m_menuIdFwToggle, TQIconSet(m_menuIconStart, TQIconSet::Small), i18n("Enable"));
    }
    m_menu->setItemEnabled(m_menuIdFwToggle, Nodes::instance()->count() <= 1);

    updateIcon();
}

void SysTray::onShowMain()
{
    emit showMainWindow();
}

void SysTray::onEnableInterception()
{
    emit enableInterception(true);
}

void SysTray::onDisableInterception()
{
    emit enableInterception(false);
}

void SysTray::onHelp()
{
    // Open help URL in browser
    Config::openUrl(Config::HELP_CONFIG_URL);
}

void SysTray::onQuit()
{
    emit quitRequested();
}

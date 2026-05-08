#ifndef OPENSNITCH_SYSTRAY_H
#define OPENSNITCH_SYSTRAY_H

#include <ntqobject.h>
#include <ntqpixmap.h>
#include <ksystemtray.h>
#include <tdepopupmenu.h>

// Complexity: O(1) for all operations
// Dependencies: TDE system tray (KSystemTray)
// Alignment: none required

// Custom KSystemTray subclass that intercepts right-click
// to show a clean custom menu without TDE's injected "Quit" entry.
class OpenSnitchTray : public KSystemTray {
    TQ_OBJECT

public:
    OpenSnitchTray(TQWidget* parent = 0, const char* name = 0)
        : KSystemTray(parent, name)
    {
        customMenu = new TDEPopupMenu(parent);
    }

    TDEPopupMenu* customMenu;

signals:
    void leftClicked();

protected:
    void mousePressEvent(TQMouseEvent* e) {
        if (e->button() == TQt::RightButton) return;
        KSystemTray::mousePressEvent(e);
    }

    void mouseReleaseEvent(TQMouseEvent* e) {
        if (e->button() == TQt::RightButton) {
            customMenu->popup(e->globalPos());
            return;
        }
        if (e->button() == TQt::LeftButton) {
            emit leftClicked();
            return;
        }
        KSystemTray::mouseReleaseEvent(e);
    }
};

class SysTray : public TQObject {
    TQ_OBJECT

public:
    SysTray(TQObject* parent = 0, const char* name = 0);
    ~SysTray();

    void show();

    void setIconConnected();
    void setIconDisconnected();
    void setIconPaused();

signals:
    void toggleMainWindow();
    void showMainWindow();
    void enableInterception(bool enable);
    void quitRequested();

public slots:
    void setFirewallEnabled(bool enabled);
    void setDaemonConnected(bool connected);
    void setIconAlert();
    void restoreIcon();
    void reloadIcons();

private slots:
    void onTrayClicked()
    {
        emit toggleMainWindow();
    }
    void onShowMain();
    void onEnableInterception();
    void onDisableInterception();
    void onHelp();
    void onQuit();
    void onDaemonSubscribed(const TQString& peer, bool firewallRunning);

private:
    void setupIcons();
    void updateIcon();

    OpenSnitchTray* m_tray;
    TDEPopupMenu* m_menu;

    TQPixmap m_iconOff;
    TQPixmap m_iconWhite;
    TQPixmap m_iconPause;
    TQPixmap m_iconAlert;

    TQPixmap m_menuIconStart;
    TQPixmap m_menuIconPause;
    TQPixmap m_menuIconHelp;
    TQPixmap m_menuIconQuit;

    bool m_fwEnabled;
    bool m_connected;

    int m_menuIdMain;
    int m_menuIdFwToggle;
    int m_menuIdHelp;
    int m_menuIdQuit;
};

#endif // OPENSNITCH_SYSTRAY_H

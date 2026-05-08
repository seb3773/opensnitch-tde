#ifndef OPENSNITCH_PROCESS_DETAILS_DIALOG_H
#define OPENSNITCH_PROCESS_DETAILS_DIALOG_H

#include <ntqdialog.h>
#include <ntqstring.h>
#include <ntqmap.h>

#include "tqtjson.h"

class TQLabel;
class TQComboBox;
class TQPushButton;
class TQTabWidget;
class TQTextEdit;

class ProcessDetailsDialog : public TQDialog {
    TQ_OBJECT

public:
    ProcessDetailsDialog(class GRpcServer* srv, TQWidget* parent = 0, const char* name = 0);
    ~ProcessDetailsDialog();

    void monitor(const TQMap<TQString, TQString>& pidToNode);
    void handleNotificationReply(const TQString& peer, unsigned long long id, int code, const TQString& data);

private slots:
    void onCloseClicked();
    void onActionToggled();
    void onPidChanged(int idx);
    void onTabChanged(TQWidget*);

private:
    void resetUi();
    void setRunning(int yes);
    void startMonitoring();
    void stopMonitoring();

    void loadDataJson(const TQString& json);
    void loadAppIconAndName(const TQString& procPath);
    void renderTabFromJson(int tabIdx, const TQtJsonObject& proc);
    static TQString wrapAtSpaces(const TQString& s, int maxCols);

private:
    class GRpcServer* m_server;

    TQLabel* m_icon;
    TQLabel* m_name;
    TQLabel* m_args;
    TQLabel* m_cwd;
    TQLabel* m_statm;

    TQTabWidget* m_tabs;
    TQTextEdit* m_textStatus;
    TQTextEdit* m_textOpenFiles;
    TQTextEdit* m_textIO;
    TQTextEdit* m_textMaps;
    TQTextEdit* m_textStack;
    TQTextEdit* m_textEnv;

    TQComboBox* m_pidCombo;
    TQPushButton* m_actionBtn;
    TQPushButton* m_closeBtn;

    TQMap<TQString, TQString> m_pids;
    TQMap<unsigned long long, int> m_notificationsSent;
    TQString m_pid;
    TQString m_node;
    TQString m_lastJson;
    unsigned long long m_lastNid;
    int m_running;
    int m_dataLoaded;
};

#endif // OPENSNITCH_PROCESS_DETAILS_DIALOG_H

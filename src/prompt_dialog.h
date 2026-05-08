#ifndef OPENSNITCH_PROMPT_DIALOG_H
#define OPENSNITCH_PROMPT_DIALOG_H

#include <ntqdialog.h>
#include <ntqstring.h>
#include <ntqlabel.h>
#include <ntqpushbutton.h>
#include <ntqcombobox.h>
#include <ntqcheckbox.h>
#include <ntqprogressbar.h>
#include <ntqtimer.h>
#include <ntqdatetime.h>

#include "ui.pb.h"

// Complexity: O(1) for all operations
// Dependencies: TQt3 widgets, protobuf, KIconLoader
// Alignment: none required
// Thread safety: GUI thread only, uses TQCustomEvent for cross-thread

class TQVBoxLayout;
class TQHBoxLayout;
class TQGridLayout;
class TQFrame;

class PromptDialog : public TQDialog {
    TQ_OBJECT

public:
    PromptDialog(TQWidget* parent = 0, const char* name = 0);
    ~PromptDialog();

    // Show the prompt and block until user responds or timeout
    // Returns the rule decided by the user, or a default rule on timeout
    protocol::Rule promptUser(const protocol::Connection& conn, bool isLocal, const TQString& peer);

    // Duration labels for combo box (matching original UI)
    static const char* DURATION_LABELS[];
    static int DURATION_COUNT;

    // Target labels for combo box (matching original UI)
    static const char* TARGET_LABELS[];
    static int TARGET_COUNT;

protected:
    void closeEvent(TQCloseEvent* e);
    void keyPressEvent(TQKeyEvent* e);

private slots:
    void onAllow();
    void onDeny();
    void onReject();
    void onTick();
    void onAdvancedToggled(bool on);

private:
    void setupUi();
    void populateFields(const protocol::Connection& conn, bool isLocal);
    protocol::Rule buildRule();
    void applyConfigDefaults();
    TQPixmap loadAppIcon(const TQString& procPath);

    // --- Header section ---
    TQLabel* m_appIcon;
    TQLabel* m_appName;
    TQLabel* m_appPathLabel;
    TQLabel* m_argsLabel;

    // --- Separator ---
    TQFrame* m_separator;

    // --- Connection summary ---
    TQLabel* m_messageLabel;

    // --- Details grid ---
    TQLabel* m_lblCwd;
    TQLabel* m_lblSrcIP;
    TQLabel* m_lblDstIP;
    TQLabel* m_lblDstPort;
    TQLabel* m_lblUID;
    TQLabel* m_lblPID;
    TQLabel* m_valCwd;
    TQLabel* m_valSrcIP;
    TQLabel* m_valDstIP;
    TQLabel* m_valDstPort;
    TQLabel* m_valUID;
    TQLabel* m_valPID;

    // Advanced checkboxes (hidden by default)
    TQCheckBox* m_checkDstIP;
    TQCheckBox* m_checkDstPort;
    TQCheckBox* m_checkUserID;

    // Advanced destination IP selector (hidden by default)
    TQComboBox* m_whatIPCombo;

    // --- Bottom action bar ---
    TQComboBox* m_whatCombo;
    TQComboBox* m_durationCombo;
    TQPushButton* m_denyBtn;
    TQPushButton* m_allowBtn;
    TQPushButton* m_advancedBtn; // checkable "+" button

    // --- Timeout ---
    TQTimer* m_tickTimer;
    int m_tick;
    int m_maxTick;
    bool m_timedOut;

    // Current connection being prompted
    protocol::Connection m_currentConn;
    TQString m_currentPeer;
    bool m_currentIsLocal;
    protocol::Rule m_resultRule;
    bool m_hasResult;
};

#endif // OPENSNITCH_PROMPT_DIALOG_H

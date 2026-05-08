#ifndef OPENSNITCH_UI_CONTROLLER_H
#define OPENSNITCH_UI_CONTROLLER_H

#include <ntqwidget.h>
#include <ntqevent.h>
#include <ntqvaluelist.h>
#include <ntqmap.h>

#include "grpc_server.h"
#include "prompt_dialog.h"
#include "rules.h"
#include "nodes.h"

// UIController receives AskRule and PingStats events from the gRPC thread,
// shows the prompt dialog, processes stats, and signals the main window.
// It must be a TQWidget to receive custom events via TQApplication::postEvent.

class UIController : public TQWidget {
    TQ_OBJECT

public:
    UIController(TQWidget* parent = 0, const char* name = 0);
    ~UIController();

    void setPromptDialog(PromptDialog* dlg) { m_promptDialog = dlg; }

signals:
    void statsUpdated(bool needRefresh, int connections, int dropped, int rules, const TQString& uptime);
    void daemonSubscribed(const TQString& peer, bool firewallRunning);
    void notificationReply(const TQString& peer, unsigned long long id, int code, const TQString& data);
    void alertRequested();
    void alertFinished();

protected:
    void customEvent(TQCustomEvent* ev);

private:
    void populateStats(const TQString& peer, const protocol::Statistics& stats);

    PromptDialog* m_promptDialog;
    // Deduplication: track last seen unixnano per node addr
    TQMap<TQString, TQValueList<int64_t> > m_lastStats;
    // Detail table dedup: last items per table per addr
    TQMap<TQString, TQMap<TQString, TQMap<TQString, uint64_t> > > m_lastItems;
};

#endif // OPENSNITCH_UI_CONTROLLER_H

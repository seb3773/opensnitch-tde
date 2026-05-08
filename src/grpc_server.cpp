#include "grpc_server.h"
#include "config.h"
#include "nodes.h"
#include "rules.h"

#include <ntqapplication.h>
#include <grpcpp/security/server_credentials.h>

// Global pointer set from main.cpp
extern TQApplication* g_app;
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

// ============================================================================
// PingCallData
// ============================================================================
PingCallData::PingCallData(protocol::UI::AsyncService* service, grpc::ServerCompletionQueue* cq)
    : m_service(service), m_cq(cq),
      m_responder(&m_ctx),
      m_state(CREATE)
{
    m_tagAccept.type = RpcTag::ACCEPT;
    m_tagAccept.call = this;
    m_tagFinish.type = RpcTag::FINISH;
    m_tagFinish.call = this;
    proceed(true);
}

void PingCallData::proceed(bool ok, RpcTag::Type)
{
    if (!ok && m_state != FINISH) {
        delete this;
        return;
    }

    switch (m_state) {
    case CREATE:
        m_service->RequestPing(&m_ctx, &m_request, &m_responder, m_cq, m_cq, &m_tagAccept);
        m_state = PROCESS;
        break;

    case PROCESS:
        // Spawn a new Ping handler for next request
        new PingCallData(m_service, m_cq);

        // Post stats event to GUI thread (matches Python _update_stats_trigger)
        {
            TQString peer = TQString(m_ctx.peer().c_str());
            if (g_app && g_app->mainWidget())
                TQApplication::postEvent(g_app->mainWidget(),
                    new PingStatsEvent(peer, m_request));
        }
        m_reply.set_id(m_request.id());
        m_responder.Finish(m_reply, grpc::Status::OK, &m_tagFinish);
        m_state = FINISH;
        break;

    case FINISH:
        delete this;
        break;
    }
}

void PingCallData::cancel()
{
    m_ctx.TryCancel();
    delete this;
}

// ============================================================================
// AskRuleCallData
// ============================================================================
AskRuleCallData::AskRuleCallData(protocol::UI::AsyncService* service,
                                 grpc::ServerCompletionQueue* cq,
                                 GRpcServer* server)
    : m_service(service), m_cq(cq), m_server(server),
      m_responder(&m_ctx),
      m_state(CREATE),
      m_finishCompleted(0),
      m_doneNotified(0),
      m_finishStarted(0),
      m_guiHolding(0)
{
    m_tagAccept.type = RpcTag::ACCEPT;
    m_tagAccept.call = this;
    m_tagFinish.type = RpcTag::FINISH;
    m_tagFinish.call = this;
    m_tagDone.type = RpcTag::DONE;
    m_tagDone.call = this;
    proceed(true);
}

void AskRuleCallData::releaseFromGui()
{
    // Called only from GUI thread.
    m_guiHolding = 0;
    if (m_doneNotified && m_finishCompleted)
        delete this;
}

void AskRuleCallData::proceed(bool ok, RpcTag::Type tagType)
{
    // Note: AskRule can enter WAITING and remain there until the GUI answers.
    // If the RPC gets cancelled while WAITING, there may be no further CQ events
    // unless we register AsyncNotifyWhenDone().
    if (tagType == RpcTag::DONE) {
        m_doneNotified = 1;

        // If we never handed ownership to the GUI (no posted event), delete now.
        if (!m_guiHolding && !m_finishStarted && m_state != WAITING) {
            delete this;
            return;
        }

        // Otherwise: GUI may still call finishWithRule(). Deletion will happen
        // when both FINISH and DONE are seen and GUI releases.
        if (!m_guiHolding && m_finishCompleted) {
            delete this;
            return;
        }
        return;
    }

    if (!ok) {
        if (m_state != FINISH) {
            delete this;
            return;
        }
    }

    if (tagType == RpcTag::FINISH) {
        m_finishCompleted = 1;
        if (m_doneNotified && !m_guiHolding) {
            delete this;
            return;
        }
        return;
    }

    switch (m_state) {
    case CREATE:
        m_service->RequestAskRule(&m_ctx, &m_request, &m_responder, m_cq, m_cq, &m_tagAccept);
        m_state = PROCESS;
        break;

    case PROCESS:
        // Spawn new handler for next request
        new AskRuleCallData(m_service, m_cq, m_server);

        // Ensure we get notified if the client cancels while we're WAITING.
        m_ctx.AsyncNotifyWhenDone(&m_tagDone);

        // GUI holds this pointer until it calls finishWithRule() and releaseFromGui().
        m_guiHolding = 1;

        // Post a custom event to the GUI thread to show the prompt dialog.
        // The CallData stays alive in WAITING state until the GUI responds.
        {
            AskRuleEvent* ev = new AskRuleEvent(this);
            if (g_app && g_app->mainWidget())
                TQApplication::postEvent(g_app->mainWidget(), ev);
        }
        m_state = WAITING;
        break;

    case WAITING:
        // Do nothing - waiting for GUI thread to call finishWithRule()
        break;

    case FINISH:
        delete this;
        break;
    }
}

void AskRuleCallData::cancel()
{
    m_ctx.TryCancel();
    delete this;
}

void AskRuleCallData::finishWithRule(const protocol::Rule& rule)
{
    if (m_doneNotified) {
        // Client cancelled; do not attempt Finish on a dead stream.
        m_finishStarted = 0;
        m_finishCompleted = 1;
        return;
    }

    m_finishStarted = 1;
    m_reply = rule;
    m_responder.Finish(m_reply, grpc::Status::OK, &m_tagFinish);
    m_state = FINISH;
}

// ============================================================================
// SubscribeCallData
// ============================================================================
SubscribeCallData::SubscribeCallData(protocol::UI::AsyncService* service,
                                     grpc::ServerCompletionQueue* cq,
                                     GRpcServer* server)
    : m_service(service), m_cq(cq), m_server(server),
      m_responder(&m_ctx),
      m_state(CREATE)
{
    m_tagAccept.type = RpcTag::ACCEPT;
    m_tagAccept.call = this;
    m_tagFinish.type = RpcTag::FINISH;
    m_tagFinish.call = this;
    proceed(true);
}

void SubscribeCallData::proceed(bool ok, RpcTag::Type)
{
    if (!ok && m_state != FINISH) {
        delete this;
        return;
    }

    switch (m_state) {
    case CREATE:
        m_service->RequestSubscribe(&m_ctx, &m_request, &m_responder, m_cq, m_cq, &m_tagAccept);
        m_state = PROCESS;
        break;

    case PROCESS:
        // Spawn new handler
        new SubscribeCallData(m_service, m_cq, m_server);

        // Register the node and post event to GUI thread
        {
            TQString peer = TQString(m_ctx.peer().c_str());
            Nodes::instance()->add(peer, m_request);

            // Also import rules from this node config so the Rules tab shows them.
            // Normalize peer address to "proto:addr" (matches UIController).
            TQString proto, addr;
            Nodes::splitPeer(peer, proto, addr);
            const TQString nodeAddr = proto + ":" + addr;
            Rules::instance()->addRules(nodeAddr, m_request.rules());

            // Post subscribe event to GUI thread with firewall state
            if (g_app && g_app->mainWidget())
                TQApplication::postEvent(g_app->mainWidget(),
                    new SubscribeEvent(peer, m_request.isfirewallrunning()));

            // Overwrite DefaultAction in the config before replying
            protocol::ClientConfig reply = m_request;
            Config* cfg = Config::get();
            // Parse config JSON, set DefaultAction, serialize back
            // For MVP, just return the config as-is
            m_reply = reply;
        }
        m_responder.Finish(m_reply, grpc::Status::OK, &m_tagFinish);
        m_state = FINISH;
        break;

    case FINISH:
        delete this;
        break;
    }
}

void SubscribeCallData::cancel()
{
    m_ctx.TryCancel();
    delete this;
}

// ============================================================================
// NotificationsCallData (bidirectional streaming)
// ============================================================================
NotificationsCallData::NotificationsCallData(protocol::UI::AsyncService* service,
                                             grpc::ServerCompletionQueue* cq,
                                             grpc::ServerCompletionQueue* notifyCq,
                                             GRpcServer* server)
    : m_service(service), m_cq(cq), m_notifyCq(notifyCq), m_server(server),
      m_stream(&m_ctx),
      m_state(CREATE), m_writePending(false)
{
    pthread_mutex_init(&m_writeLock, 0);

    m_tagAccept.type = RpcTag::ACCEPT;
    m_tagAccept.call = this;
    m_tagRead.type = RpcTag::READ;
    m_tagRead.call = this;
    m_tagWrite.type = RpcTag::WRITE;
    m_tagWrite.call = this;
    m_tagFinish.type = RpcTag::FINISH;
    m_tagFinish.call = this;
    m_tagAlarm.type = RpcTag::ALARM;
    m_tagAlarm.call = this;

    proceed(true);
}

NotificationsCallData::~NotificationsCallData()
{
    pthread_mutex_destroy(&m_writeLock);
}

void NotificationsCallData::proceed(bool ok, RpcTag::Type tagType)
{
    if (!ok) {
        // grpc::Alarm::Set() cancels any previous alarm; the cancelled alarm
        // completion is delivered with ok=false. Treat it as a no-op.
        if (tagType == RpcTag::ALARM)
            return;

        if (m_state != DONE) {
            m_server->unregisterNotifications(this);
            m_state = DONE;
        }
        delete this;
        return;
    }

    switch (m_state) {
    case CREATE:
        // Accept the Notifications streaming RPC
        m_service->RequestNotifications(&m_ctx, &m_stream, m_cq, m_notifyCq, &m_tagAccept);
        m_state = CONNECTED;
        break;

    case CONNECTED:
        // Accept completed, spawn new handler, register, start reading
        new NotificationsCallData(m_service, m_cq, m_notifyCq, m_server);
        m_server->registerNotifications(this);
        m_stream.Read(&m_readBuffer, &m_tagRead);
        m_state = ACTIVE;
        break;

    case ACTIVE:
        // Dispatch based on which tag completed
        if (tagType == RpcTag::ALARM) {
            // Alarm fired: start the actual Write from the CQ thread
            pthread_mutex_lock(&m_writeLock);
            if (!m_writePending) {
                pthread_mutex_unlock(&m_writeLock);
                break;
            }
            protocol::Notification notif = m_pendingNotif;
            pthread_mutex_unlock(&m_writeLock);
            m_stream.Write(notif, &m_tagWrite);
        } else if (tagType == RpcTag::WRITE) {
            // Write completed, Read is still pending
            pthread_mutex_lock(&m_writeLock);
            m_writePending = false;
            pthread_mutex_unlock(&m_writeLock);
        } else if (tagType == RpcTag::READ) {
            // Forward notification reply to GUI thread (used e.g. by process monitor)
            {
                TQString peer = peerAddr();
                TQString proto, addr;
                Nodes::splitPeer(peer, proto, addr);
                peer = proto + ":" + addr;
                if (g_app && g_app->mainWidget())
                    TQApplication::postEvent(g_app->mainWidget(),
                        new NotificationReplyEvent(peer, m_readBuffer));
            }
            // Read completed, restart reading
            m_stream.Read(&m_readBuffer, &m_tagRead);
        }
        break;

    case DONE:
        delete this;
        break;
    }
}

void NotificationsCallData::cancel()
{
    m_ctx.TryCancel();
    m_server->unregisterNotifications(this);
}

void NotificationsCallData::sendNotification(const protocol::Notification& notif)
{
    pthread_mutex_lock(&m_writeLock);
    if (m_writePending) {
        pthread_mutex_unlock(&m_writeLock);
        return;
    }
    m_pendingNotif = notif;
    m_writePending = true;
    pthread_mutex_unlock(&m_writeLock);

    // Schedule the Write from the CQ thread via alarm.
    // m_tagAlarm has type=ALARM so proceed() can distinguish it from
    // a WRITE completion and start the actual m_stream.Write() call.
    m_alarm.Set(m_notifyCq, gpr_now(GPR_CLOCK_MONOTONIC), &m_tagAlarm);
}

TQString NotificationsCallData::peerAddr() const
{
    return TQString(m_ctx.peer().c_str());
}

// ============================================================================
// GRpcServer
// ============================================================================
GRpcServer::GRpcServer()
    : m_cq(0), m_notifyCq(0), m_running(false)
{
    pthread_mutex_init(&m_notifsLock, 0);
}

GRpcServer::~GRpcServer()
{
    stop();
    pthread_mutex_destroy(&m_notifsLock);
}

bool GRpcServer::start(const TQString& socket, int maxWorkers)
{
    grpc::ServerBuilder builder;

    // Listen on the given socket
    TQString addr = socket;
    if (addr.startsWith("unix://")) {
        // gRPC expects "unix://path" format
    } else if (addr.startsWith("unix-abstract:")) {
        // Transform to what gRPC understands
        // gRPC C++ supports "unix:path" for filesystem and abstract
    }
    builder.AddListeningPort(addr.latin1(), grpc::InsecureServerCredentials());

    // Keepalive options
    Config* cfg = Config::get();
    int keepalive = cfg->getInt(Config::KEY_SERVER_KEEPALIVE, 5000);
    int keepaliveTimeout = cfg->getInt(Config::KEY_SERVER_KEEPALIVE_TIMEOUT, 20000);
    int maxMsgLen = cfg->getMaxMsgLength();

    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, keepalive);
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, keepaliveTimeout);
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    builder.AddChannelArgument(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, maxMsgLen);
    builder.AddChannelArgument(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, maxMsgLen);

    builder.RegisterService(&m_service);

    m_cq = builder.AddCompletionQueue().release();
    m_notifyCq = builder.AddCompletionQueue().release();

    m_server = builder.BuildAndStart();
    if (!m_server) {
        fprintf(stderr, "Failed to start gRPC server on %s\n", socket.latin1());
        return false;
    }

    fprintf(stdout, "gRPC server listening on %s\n", socket.latin1());

    // Restrict unix socket permissions
    if (socket.startsWith("unix://")) {
        TQString path = socket.mid(7);
        if (!path.isEmpty() && access(path.latin1(), F_OK) == 0)
            ::chmod(path.latin1(), 0640);
    }

    m_running = true;

    // Start CQ threads
    pthread_create(&m_cqThread, 0, cqThreadFunc, this);
    pthread_create(&m_notifyCqThread, 0, notifyCqThreadFunc, this);

    // Init accept calls
    initAccepts();

    return true;
}

void GRpcServer::stop()
{
    m_running = false;

    if (m_server) {
        m_server->Shutdown();
        m_server.reset();
    }
    if (m_cq) {
        m_cq->Shutdown();
    }
    if (m_notifyCq) {
        m_notifyCq->Shutdown();
    }

    // Wait for threads
    if (m_cqThread)
        pthread_join(m_cqThread, 0);
    if (m_notifyCqThread)
        pthread_join(m_notifyCqThread, 0);
}

void GRpcServer::initAccepts()
{
    // Create one CallData for each RPC to start accepting
    new PingCallData(&m_service, m_cq);
    new AskRuleCallData(&m_service, m_cq, this);
    new SubscribeCallData(&m_service, m_cq, this);
    new NotificationsCallData(&m_service, m_cq, m_notifyCq, this);
}

void* GRpcServer::cqThreadFunc(void* arg)
{
    GRpcServer* self = static_cast<GRpcServer*>(arg);
    void* tag;
    bool ok = false;

    while (self->m_running && self->m_cq->Next(&tag, &ok)) {
        RpcTag* rpcTag = static_cast<RpcTag*>(tag);
        CallData* call = static_cast<CallData*>(rpcTag->call);
        call->proceed(ok, rpcTag->type);
    }

    return 0;
}

void* GRpcServer::notifyCqThreadFunc(void* arg)
{
    GRpcServer* self = static_cast<GRpcServer*>(arg);
    void* tag;
    bool ok = false;

    while (self->m_running && self->m_notifyCq->Next(&tag, &ok)) {
        RpcTag* rpcTag = static_cast<RpcTag*>(tag);
        CallData* call = static_cast<CallData*>(rpcTag->call);
        call->proceed(ok, rpcTag->type);
    }

    return 0;
}

void GRpcServer::broadcastNotification(const protocol::Notification& notif)
{
    pthread_mutex_lock(&m_notifsLock);
    for (TQMap<TQString, NotificationsCallData*>::ConstIterator it = m_activeNotifs.begin();
         it != m_activeNotifs.end(); ++it) {
        it.data()->sendNotification(notif);
    }
    pthread_mutex_unlock(&m_notifsLock);
}

void GRpcServer::sendNotification(const TQString& addr, const protocol::Notification& notif)
{
    pthread_mutex_lock(&m_notifsLock);
    if (m_activeNotifs.contains(addr))
        m_activeNotifs[addr]->sendNotification(notif);
    pthread_mutex_unlock(&m_notifsLock);
}

bool GRpcServer::hasNotificationStream(const TQString& addr) const
{
    int ok = 0;
    pthread_mutex_lock(&m_notifsLock);
    ok = m_activeNotifs.contains(addr) ? 1 : 0;
    pthread_mutex_unlock(&m_notifsLock);
    return ok ? true : false;
}

void GRpcServer::registerNotifications(NotificationsCallData* nd)
{
    pthread_mutex_lock(&m_notifsLock);
    // Normalize to the same "proto:addr" format used by Nodes/UI.
    TQString peer = nd->peerAddr();
    TQString proto, addr;
    Nodes::splitPeer(peer, proto, addr);
    const TQString key = proto + ":" + addr;
    m_activeNotifs[key] = nd;
    pthread_mutex_unlock(&m_notifsLock);
}

void GRpcServer::unregisterNotifications(NotificationsCallData* nd)
{
    pthread_mutex_lock(&m_notifsLock);
    TQString peer = nd->peerAddr();
    TQString proto, addr;
    Nodes::splitPeer(peer, proto, addr);
    const TQString key = proto + ":" + addr;
    if (m_activeNotifs.contains(key) && m_activeNotifs[key] == nd)
        m_activeNotifs.remove(key);
    pthread_mutex_unlock(&m_notifsLock);
}

#ifndef OPENSNITCH_GRPC_SERVER_H
#define OPENSNITCH_GRPC_SERVER_H

#include <ntqstring.h>
#include <ntqmap.h>
#include <ntqevent.h>
#include <ntqobject.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/alarm.h>

#include <pthread.h>
#include <atomic>

#include "ui.grpc.pb.h"

// Complexity: O(1) per RPC event, O(n) for completion queue loop
// Dependencies: gRPC C++, protobuf
// Alignment: none required
// Thread safety: cq loop runs in its own thread, posts TQCustomEvent to GUI

class UIServiceImpl;

// Tag structure for completion queue correlation
struct RpcTag {
    enum Type {
        ACCEPT = 0,
        READ   = 1,
        WRITE  = 2,
        FINISH = 3,
        ALARM  = 4,
        DONE   = 5
    };
    Type type;
    void* call;  // pointer to the call data
};

// Custom event type IDs for posting events to the GUI thread.
// Using enum to avoid ODR issues from static const in headers.
enum {
    AskRuleEventId            = 1000,
    PingStatsEventId          = 1001,
    SubscribeEventId          = 1002,
    NotificationReplyEventId  = 1003
};

class AskRuleEvent : public TQCustomEvent {
public:
    AskRuleEvent(class AskRuleCallData* callData)
        : TQCustomEvent(AskRuleEventId), m_callData(callData) {}

    AskRuleCallData* callData() const { return m_callData; }

private:
    AskRuleCallData* m_callData;
};

class PingStatsEvent : public TQCustomEvent {
public:
    PingStatsEvent(const TQString& peer, const protocol::PingRequest& request)
        : TQCustomEvent(PingStatsEventId), m_peer(peer), m_request(request) {}

    TQString peer() const { return m_peer; }
    const protocol::PingRequest& request() const { return m_request; }

private:
    TQString m_peer;
    protocol::PingRequest m_request;
};

class SubscribeEvent : public TQCustomEvent {
public:
    SubscribeEvent(const TQString& peer, bool firewallRunning)
        : TQCustomEvent(SubscribeEventId), m_peer(peer), m_firewallRunning(firewallRunning) {}

    TQString peer() const { return m_peer; }
    bool firewallRunning() const { return m_firewallRunning; }

private:
    TQString m_peer;
    bool m_firewallRunning;
};

class NotificationReplyEvent : public TQCustomEvent {
public:
    NotificationReplyEvent(const TQString& peer, const protocol::NotificationReply& reply)
        : TQCustomEvent(NotificationReplyEventId), m_peer(peer), m_reply(reply) {}

    TQString peer() const { return m_peer; }
    const protocol::NotificationReply& reply() const { return m_reply; }

private:
    TQString m_peer;
    protocol::NotificationReply m_reply;
};

// Base class for async RPC call state
class CallData {
public:
    virtual ~CallData() {}
    virtual void proceed(bool ok, RpcTag::Type tagType = RpcTag::ACCEPT) = 0;
    virtual void cancel() = 0;
};

// --- Ping RPC ---
class PingCallData : public CallData {
public:
    PingCallData(protocol::UI::AsyncService* service, grpc::ServerCompletionQueue* cq);
    void proceed(bool ok, RpcTag::Type tagType = RpcTag::ACCEPT) override;
    void cancel() override;

private:
    protocol::UI::AsyncService* m_service;
    grpc::ServerCompletionQueue* m_cq;
    grpc::ServerContext m_ctx;
    protocol::PingRequest m_request;
    protocol::PingReply m_reply;
    grpc::ServerAsyncResponseWriter<protocol::PingReply> m_responder;
    RpcTag m_tagAccept;
    RpcTag m_tagFinish;
    enum { CREATE, PROCESS, FINISH } m_state;
};

// --- AskRule RPC ---
class AskRuleCallData : public CallData {
public:
    AskRuleCallData(protocol::UI::AsyncService* service, grpc::ServerCompletionQueue* cq,
                    class GRpcServer* server);
    void proceed(bool ok, RpcTag::Type tagType = RpcTag::ACCEPT) override;
    void cancel() override;

    // Called from GUI thread after user responds to prompt dialog
    void finishWithRule(const protocol::Rule& rule);

    // Called from GUI thread once it is done accessing this CallData.
    void releaseFromGui();

    const protocol::Connection& connection() const { return m_request; }
    TQString peerAddr() const { return TQString(m_ctx.peer().c_str()); }

private:
    protocol::UI::AsyncService* m_service;
    grpc::ServerCompletionQueue* m_cq;
    GRpcServer* m_server;
    grpc::ServerContext m_ctx;
    protocol::Connection m_request;
    protocol::Rule m_reply;
    grpc::ServerAsyncResponseWriter<protocol::Rule> m_responder;
    RpcTag m_tagAccept;
    RpcTag m_tagFinish;
    RpcTag m_tagDone;
    enum { CREATE, PROCESS, WAITING, FINISH } m_state;

    std::atomic<int> m_finishCompleted;
    std::atomic<int> m_doneNotified;

    std::atomic<int> m_finishStarted;
    std::atomic<int> m_guiHolding;
};

// --- Subscribe RPC ---
class SubscribeCallData : public CallData {
public:
    SubscribeCallData(protocol::UI::AsyncService* service, grpc::ServerCompletionQueue* cq,
                      GRpcServer* server);
    void proceed(bool ok, RpcTag::Type tagType = RpcTag::ACCEPT) override;
    void cancel() override;

private:
    protocol::UI::AsyncService* m_service;
    grpc::ServerCompletionQueue* m_cq;
    GRpcServer* m_server;
    grpc::ServerContext m_ctx;
    protocol::ClientConfig m_request;
    protocol::ClientConfig m_reply;
    grpc::ServerAsyncResponseWriter<protocol::ClientConfig> m_responder;
    RpcTag m_tagAccept;
    RpcTag m_tagFinish;
    enum { CREATE, PROCESS, FINISH } m_state;
};

// --- Notifications bidirectional streaming RPC ---
class NotificationsCallData : public CallData {
public:
    NotificationsCallData(protocol::UI::AsyncService* service, grpc::ServerCompletionQueue* cq,
                          grpc::ServerCompletionQueue* notifyCq, GRpcServer* server);
    void proceed(bool ok, RpcTag::Type tagType = RpcTag::ACCEPT) override;
    void cancel() override;

    // Queue a notification to be sent to this client
    void sendNotification(const protocol::Notification& notif);
    TQString peerAddr() const;

    ~NotificationsCallData();

private:
    protocol::UI::AsyncService* m_service;
    grpc::ServerCompletionQueue* m_cq;
    grpc::ServerCompletionQueue* m_notifyCq;
    GRpcServer* m_server;
    grpc::ServerContext m_ctx;
    protocol::NotificationReply m_readBuffer;
    grpc::ServerAsyncReaderWriter<protocol::Notification, protocol::NotificationReply> m_stream;
    RpcTag m_tagAccept;
    RpcTag m_tagRead;
    RpcTag m_tagWrite;
    RpcTag m_tagFinish;
    RpcTag m_tagAlarm;
    enum { CREATE, CONNECTED, ACTIVE, DONE } m_state;
    bool m_writePending;
    protocol::Notification m_pendingNotif;
    grpc::Alarm m_alarm;
    pthread_mutex_t m_writeLock;
};

// --- The gRPC server ---
class GRpcServer
{
public:
    GRpcServer();
    ~GRpcServer();

    bool start(const TQString& socket, int maxWorkers = 20);
    void stop();

public:

    // Send notification to all connected nodes
    void broadcastNotification(const protocol::Notification& notif);
    // Send notification to a specific node
    void sendNotification(const TQString& addr, const protocol::Notification& notif);

    // Return true if a notifications stream is registered for this node key.
    bool hasNotificationStream(const TQString& addr) const;

    // Track active notification streams
    void registerNotifications(NotificationsCallData* nd);
    void unregisterNotifications(NotificationsCallData* nd);

    // Access the async service (for CallData constructors)
    protocol::UI::AsyncService* service() { return &m_service; }
    grpc::ServerCompletionQueue* cq() { return m_cq; }
    grpc::ServerCompletionQueue* notifyCq() { return m_notifyCq; }

private:
    static void* cqThreadFunc(void* arg);
    static void* notifyCqThreadFunc(void* arg);

    void initAccepts();

    protocol::UI::AsyncService m_service;
    std::unique_ptr<grpc::Server> m_server;
    grpc::ServerCompletionQueue* m_cq;
    grpc::ServerCompletionQueue* m_notifyCq;

    pthread_t m_cqThread;
    pthread_t m_notifyCqThread;
    bool m_running;
    bool m_threadsStarted;

    // Active notification streams (one per connected daemon)
    mutable pthread_mutex_t m_notifsLock;
    TQMap<TQString, NotificationsCallData*> m_activeNotifs;
};

#endif // OPENSNITCH_GRPC_SERVER_H

#ifndef OPENSNITCH_NODES_H
#define OPENSNITCH_NODES_H

#include <ntqobject.h>
#include <ntqstring.h>
#include <ntqmap.h>
#include <ntqdatetime.h>

#include "ui.pb.h"

// Complexity: O(1) for single node ops, O(n) for broadcast
// Dependencies: protobuf generated types
// Alignment: none required

class Nodes : public TQObject {
    TQ_OBJECT

public:
    static Nodes* instance();
    static void destroy();

    struct NodeData {
        TQString addr;
        bool online;
        TQDateTime lastSeen;
        protocol::ClientConfig config;
        bool hasConfig;

        NodeData() : online(false), hasConfig(false) {}
    };

    int count() const;
    Nodes::NodeData* node(const TQString& addr);
    const TQMap<TQString, NodeData>& nodes() const;

    NodeData* add(const TQString& peer, const protocol::ClientConfig& config);
    void remove(const TQString& addr);
    void setOnline(const TQString& addr, bool online);
    void updateAllOffline();

    TQString getHostname(const TQString& addr);
    TQString getConfig(const TQString& addr);
    void saveConfig(const TQString& addr, const TQString& configJson);

    static void splitPeer(const TQString& peer, TQString& proto, TQString& addr);

signals:
    void nodesUpdated(int total);

private:
    Nodes();
    ~Nodes();

    static Nodes* s_instance;
    TQMap<TQString, NodeData> m_nodes;
};

#endif // OPENSNITCH_NODES_H

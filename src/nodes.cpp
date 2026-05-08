#include "nodes.h"

Nodes* Nodes::s_instance = 0;

Nodes* Nodes::instance()
{
    if (!s_instance)
        s_instance = new Nodes();
    return s_instance;
}

void Nodes::destroy()
{
    delete s_instance;
    s_instance = 0;
}

Nodes::Nodes()  : TQObject(0) {}
Nodes::~Nodes() {}

int Nodes::count() const
{
    return (int)m_nodes.count();
}

Nodes::NodeData* Nodes::node(const TQString& addr)
{
    if (m_nodes.contains(addr))
        return &m_nodes[addr];
    return 0;
}

const TQMap<TQString, Nodes::NodeData>& Nodes::nodes() const
{
    return m_nodes;
}

Nodes::NodeData* Nodes::add(const TQString& peer, const protocol::ClientConfig& config)
{
    TQString proto, addr;
    splitPeer(peer, proto, addr);
    TQString key = proto + ":" + addr;

    NodeData nd;
    nd.addr = key;
    nd.online = true;
    nd.lastSeen = TQDateTime::currentDateTime();
    nd.config = config;
    nd.hasConfig = true;

    if (m_nodes.contains(key)) {
        nd.lastSeen = TQDateTime::currentDateTime();
    }
    m_nodes[key] = nd;

    emit nodesUpdated(count());
    return &m_nodes[key];
}

void Nodes::remove(const TQString& addr)
{
    m_nodes.remove(addr);
    emit nodesUpdated(count());
}

void Nodes::setOnline(const TQString& addr, bool online)
{
    if (m_nodes.contains(addr)) {
        m_nodes[addr].online = online;
        m_nodes[addr].lastSeen = TQDateTime::currentDateTime();
    }
}

void Nodes::updateAllOffline()
{
    for (TQMap<TQString, NodeData>::Iterator it = m_nodes.begin(); it != m_nodes.end(); ++it)
        it.data().online = false;
}

TQString Nodes::getHostname(const TQString& addr)
{
    if (m_nodes.contains(addr) && m_nodes[addr].hasConfig)
        return TQString(m_nodes[addr].config.name().c_str());
    return TQString();
}

TQString Nodes::getConfig(const TQString& addr)
{
    if (m_nodes.contains(addr) && m_nodes[addr].hasConfig)
        return TQString(m_nodes[addr].config.config().c_str());
    return TQString();
}

void Nodes::saveConfig(const TQString& addr, const TQString& configJson)
{
    if (m_nodes.contains(addr) && m_nodes[addr].hasConfig)
        m_nodes[addr].config.set_config(configJson.local8Bit().data());
}

void Nodes::splitPeer(const TQString& peer, TQString& proto, TQString& addr)
{
    int idx = peer.find(':');
    if (idx >= 0) {
        proto = peer.left(idx);
        addr = peer.mid(idx + 1);
        // Handle "unix:" with empty addr
        if (proto == "unix" && addr.isEmpty())
            addr = "/local";
    } else {
        proto = peer;
        addr = TQString();
    }
}

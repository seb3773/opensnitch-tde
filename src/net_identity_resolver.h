#ifndef OPENSNITCH_NET_IDENTITY_RESOLVER_H
#define OPENSNITCH_NET_IDENTITY_RESOLVER_H

#include <ntqobject.h>
#include <ntqstring.h>
#include <ntqmutex.h>

#include <map>
#include <time.h>

class TQCustomEvent;
class ResolverThread;

// Complexity: lookup O(log n), insert O(log n)
// Dependencies: TQt3 thread primitives, libc resolver (getnameinfo)
// Notes: async reverse DNS + provider heuristics; no disk IO; non-blocking UI
struct NetIdentity
{
    TQString ip;
    TQString hostname;
    TQString provider;

    int resolved;
    int resolving;

    time_t timestamp;

    NetIdentity()
        : resolved(0), resolving(0), timestamp(0)
    {
    }
};

enum { NetIdentityResolvedEventId = 1005 }; // custom event id, must not collide

class NetIdentityResolvedEvent : public TQCustomEvent
{
public:
    NetIdentityResolvedEvent(const TQString& ip, const TQString& hostname, const TQString& provider);

    TQString ip() const { return m_ip; }
    TQString hostname() const { return m_hostname; }
    TQString provider() const { return m_provider; }

private:
    TQString m_ip;
    TQString m_hostname;
    TQString m_provider;
};

class NetIdentityResolver : public TQObject
{
    TQ_OBJECT

public:
    static NetIdentityResolver* instance();

    void setUiTarget(TQObject* target);

    // Schedule async resolution for an IP.
    // Returns 1 if queued or already resolving/resolved, 0 if ignored.
    int request(const TQString& ip);

    // Lookup cached identity. Returns 1 if found and not expired.
    int lookup(const TQString& ip, NetIdentity& out);

private:
    NetIdentityResolver();
    ~NetIdentityResolver();

    friend class ResolverThread;

    NetIdentityResolver(const NetIdentityResolver&);
    NetIdentityResolver& operator=(const NetIdentityResolver&);

    void cleanupLocked(time_t now);

private:
    class ResolverThread* m_thread;
    TQObject* m_uiTarget;

    TQMutex m_lock;
    std::map<TQString, NetIdentity> m_cache;
};

#endif

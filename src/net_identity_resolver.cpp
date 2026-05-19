#include "net_identity_resolver.h"

#include <ntqapplication.h>
#include <ntqthread.h>
#include <ntqevent.h>
#include <ntqvaluelist.h>

#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

static inline int isPrivateIPv4(uint32_t ip_be)
{
    const uint32_t ip = ntohl(ip_be);

    // 10.0.0.0/8
    if ((ip & 0xFF000000u) == 0x0A000000u)
        return 1;

    // 172.16.0.0/12
    if ((ip & 0xFFF00000u) == 0xAC100000u)
        return 1;

    // 192.168.0.0/16
    if ((ip & 0xFFFF0000u) == 0xC0A80000u)
        return 1;

    // 127.0.0.0/8
    if ((ip & 0xFF000000u) == 0x7F000000u)
        return 1;

    // 169.254.0.0/16 (link-local)
    if ((ip & 0xFFFF0000u) == 0xA9FE0000u)
        return 1;

    // 224.0.0.0/4 multicast
    if ((ip & 0xF0000000u) == 0xE0000000u)
        return 1;

    return 0;
}

struct DomainHint {
    const char* suffix;
    const char* provider;
};

static const DomainHint g_hints[] = {
    { ".1e100.net", "Google" },
    { ".googleusercontent.com", "Google" },
    { ".cloudflare.com", "Cloudflare" },
    { ".amazonaws.com", "Amazon AWS" },
    { ".akamaitechnologies.com", "Akamai" },
    { ".fastly.net", "Fastly" },
    { ".digitalocean.com", "DigitalOcean" },
    { ".microsoft.com", "Microsoft" },
    { ".azure.com", "Microsoft" },
    { ".github.com", "GitHub" },
};

static inline TQString guessProvider(const TQString& host)
{
    if (host.isEmpty())
        return TQString();

    const TQString h = host.lower();
    const size_t n = sizeof(g_hints) / sizeof(g_hints[0]);
    for (size_t i = 0; i < n; ++i) {
        if (h.endsWith(g_hints[i].suffix))
            return TQString(g_hints[i].provider);
    }

    return TQString();
}

NetIdentityResolvedEvent::NetIdentityResolvedEvent(const TQString& ip, const TQString& hostname, const TQString& provider)
    : TQCustomEvent(NetIdentityResolvedEventId),
      m_ip(ip),
      m_hostname(hostname),
      m_provider(provider)
{
}

class ResolverThread : public TQThread
{
public:
    ResolverThread(NetIdentityResolver* owner)
        : m_owner(owner)
    {
    }

    void enqueue(const TQString& ip)
    {
        if (!m_owner)
            return;

        m_owner->m_lock.lock();
        // Deduplicate and avoid parallel resolves.
        std::map<TQString, NetIdentity>::iterator it = m_owner->m_cache.find(ip);
        if (it != m_owner->m_cache.end()) {
            if (it->second.resolving) {
                m_owner->m_lock.unlock();
                return;
            }
            it->second.resolving = 1;
            it->second.resolved = 0;
            it->second.timestamp = time(0);
        } else {
            NetIdentity ni;
            ni.ip = ip;
            ni.resolving = 1;
            ni.resolved = 0;
            ni.timestamp = time(0);
            m_owner->m_cache.insert(std::make_pair(ip, ni));
        }

        // Simple FIFO queue.
        m_queue.push_back(ip);
        m_owner->m_lock.unlock();

        if (!running())
            start();
    }

protected:
    void run()
    {
        // Single worker, low CPU: poll queue with small sleep.
        for (;;) {
            TQString ip;
            m_owner->m_lock.lock();
            if (!m_queue.empty()) {
                ip = m_queue.front();
                m_queue.pop_front();
            }
            m_owner->m_lock.unlock();

            if (ip.isEmpty())
                break;

            resolveOne(ip);

            // Avoid spamming resolver.
            usleep(50 * 1000);
        }
    }

private:
    void resolveOne(const TQString& ip)
    {
        TQString hostname;
        TQString provider;

        // Skip private / special ranges (IPv4 only for now).
        struct in_addr a4;
        if (inet_pton(AF_INET, ip.latin1(), &a4) == 1) {
            if (isPrivateIPv4(a4.s_addr)) {
                finish(ip, hostname, provider);
                return;
            }

            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr = a4;

            char hostbuf[NI_MAXHOST];
            hostbuf[0] = '\0';
            const int rc = getnameinfo((struct sockaddr*)&sa, sizeof(sa),
                                       hostbuf, sizeof(hostbuf),
                                       0, 0, NI_NAMEREQD);
            if (rc == 0 && hostbuf[0] != '\0') {
                hostname = TQString::fromLocal8Bit(hostbuf);
                provider = guessProvider(hostname);
            }

            finish(ip, hostname, provider);
            return;
        }

        // IPv6: best effort reverse DNS.
        struct in6_addr a6;
        if (inet_pton(AF_INET6, ip.latin1(), &a6) == 1) {
            struct sockaddr_in6 sa6;
            memset(&sa6, 0, sizeof(sa6));
            sa6.sin6_family = AF_INET6;
            sa6.sin6_addr = a6;

            char hostbuf[NI_MAXHOST];
            hostbuf[0] = '\0';
            const int rc = getnameinfo((struct sockaddr*)&sa6, sizeof(sa6),
                                       hostbuf, sizeof(hostbuf),
                                       0, 0, NI_NAMEREQD);
            if (rc == 0 && hostbuf[0] != '\0') {
                hostname = TQString::fromLocal8Bit(hostbuf);
                provider = guessProvider(hostname);
            }

            finish(ip, hostname, provider);
            return;
        }

        // Unknown format
        finish(ip, hostname, provider);
    }

    void finish(const TQString& ip, const TQString& hostname, const TQString& provider)
    {
        const time_t now = time(0);

        m_owner->m_lock.lock();
        std::map<TQString, NetIdentity>::iterator it = m_owner->m_cache.find(ip);
        if (it != m_owner->m_cache.end()) {
            it->second.hostname = hostname;
            it->second.provider = provider;
            it->second.resolved = 1;
            it->second.resolving = 0;
            it->second.timestamp = now;
        }
        TQObject* target = m_owner->m_uiTarget;
        m_owner->m_lock.unlock();

        if (target)
            TQApplication::postEvent(target, new NetIdentityResolvedEvent(ip, hostname, provider));
    }

private:
    NetIdentityResolver* m_owner;
    TQValueList<TQString> m_queue;
};

NetIdentityResolver* NetIdentityResolver::instance()
{
    static NetIdentityResolver* inst = 0;
    if (!inst)
        inst = new NetIdentityResolver();
    return inst;
}

NetIdentityResolver::NetIdentityResolver()
    : TQObject(0),
      m_thread(0),
      m_uiTarget(0)
{
    m_thread = new ResolverThread(this);
}

NetIdentityResolver::~NetIdentityResolver()
{
    // Wait for the resolver thread to finish before deleting.
    // getnameinfo() can block for seconds, so use a reasonable timeout.
    if (m_thread && m_thread->running())
        m_thread->wait(5000);
    delete m_thread;
    m_thread = 0;
}

void NetIdentityResolver::setUiTarget(TQObject* target)
{
    m_lock.lock();
    m_uiTarget = target;
    m_lock.unlock();
}

void NetIdentityResolver::cleanupLocked(time_t now)
{
    // TTL: 1 hour.
    const time_t ttl = 3600;

    // Lazy cleanup: keep it simple.
    for (std::map<TQString, NetIdentity>::iterator it = m_cache.begin(); it != m_cache.end();) {
        if (!it->second.resolving && it->second.timestamp != 0 && (now - it->second.timestamp) > ttl) {
            std::map<TQString, NetIdentity>::iterator toErase = it;
            ++it;
            m_cache.erase(toErase);
            continue;
        }
        ++it;
    }
}

int NetIdentityResolver::lookup(const TQString& ip, NetIdentity& out)
{
    const time_t now = time(0);

    m_lock.lock();
    cleanupLocked(now);

    std::map<TQString, NetIdentity>::const_iterator it = m_cache.find(ip);
    if (it == m_cache.end()) {
        m_lock.unlock();
        return 0;
    }

    out = it->second;
    m_lock.unlock();

    if (!out.resolved)
        return 0;

    return 1;
}

int NetIdentityResolver::request(const TQString& ip)
{
    if (ip.isEmpty())
        return 0;

    const time_t now = time(0);

    m_lock.lock();
    cleanupLocked(now);

    std::map<TQString, NetIdentity>::iterator it = m_cache.find(ip);
    if (it != m_cache.end()) {
        if (it->second.resolved || it->second.resolving) {
            m_lock.unlock();
            return 1;
        }
    }
    m_lock.unlock();

    if (m_thread)
        m_thread->enqueue(ip);

    return 1;
}

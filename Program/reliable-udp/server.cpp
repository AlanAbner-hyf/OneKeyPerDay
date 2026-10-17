// Linux UDP reliable ordered messages with explicit sessions.
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

using Clock = std::chrono::steady_clock;

// 这些限制共同约束内存。窗口限定“允许提前到达多少个 ID”。
constexpr std::size_t MAX_SESSIONS = 128;
constexpr std::size_t MAX_PENDING_MESSAGES = 64;
constexpr std::size_t MAX_PENDING_BYTES = 256 * 1024;
constexpr std::uint64_t RECEIVE_WINDOW = 64;
constexpr std::size_t MAX_CONTENT_BYTES = 65507 - 49; // 包括最坏情况下的协议头
constexpr auto IDLE_TIMEOUT = std::chrono::seconds(60);
constexpr auto GAP_TIMEOUT = std::chrono::seconds(30);

bool parse_message(const char* buffer, std::size_t size,
                   std::uint32_t& id, std::string& content)
{
    if (buffer == nullptr) return false;
    std::size_t pos = 0;
    std::uint32_t parsed = 0;
    constexpr auto max_id = std::numeric_limits<std::uint32_t>::max();
    while (pos < size && buffer[pos] != ':') {
        const char ch = buffer[pos];
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<std::uint32_t>(ch - '0');
        if (parsed > (max_id - digit) / 10) return false;
        parsed = parsed * 10 + digit;
        ++pos;
    }
    if (pos == 0 || pos == size) return false;
    content.assign(buffer + pos + 1, size - pos - 1);
    id = parsed;
    return true;
}

bool valid_token(const std::string& token)
{
    if (token.size() != 32) return false;
    for (char ch : token) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}

// 128 位随机标识用于隔离会话；本教学协议没有实现身份认证。
std::string new_token()
{
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    for (int i = 0; i < 4; ++i) {
        std::uint32_t value = random();
        for (int digit = 0; digit < 8; ++digit) {
            token.push_back(hex[value & 15]);
            value >>= 4;
        }
    }
    return token;
}

struct Session {
    std::string endpoint;       // 创建会话时的 IP:Port
    std::string client_nonce;   // 区分不同客户端启动，也让 HELLO 重试保持幂等
    std::uint64_t next_id = 1;  // 用更宽的类型，交付 UINT32_MAX 后不会回绕
    std::unordered_map<std::uint32_t, std::string> pending;
    std::size_t pending_bytes = 0;
    Clock::time_point last_activity;
    Clock::time_point gap_since; // 缺口开始或最近一次有序交付的时间
};

// 空字符串表示不回复；只有 accepted_data=true 的回复才参加丢 ACK 实验。
struct Reply {
    std::string packet;
    bool accepted_data = false;
};

class SessionServer {
public:
    std::unordered_map<std::string, Session> sessions;

    // 即使没有新数据包，主循环也会每秒调用一次清理。
    void expire_sessions(Clock::time_point now)
    {
        for (auto it = sessions.begin(); it != sessions.end();) {
            const Session& session = it->second;
            const bool idle = now - session.last_activity >= IDLE_TIMEOUT;
            const bool stalled = !session.pending.empty()
                              && now - session.gap_since >= GAP_TIMEOUT;
            if (idle || stalled) {
                std::cout << "[Session expired] " << it->first
                          << (stalled ? " gap timeout" : " idle timeout") << '\n';
                // 清理整个会话，而不是删除缓存后继续认可旧会话。
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    Reply handle_packet(const std::string& endpoint, const std::string& packet,
                        Clock::time_point now)
    {
        expire_sessions(now);
        if (packet.compare(0, 6, "HELLO:") == 0) {
            const std::string nonce = packet.substr(6);
            if (!valid_token(nonce)) return {};
            for (auto& entry : sessions) {
                Session& existing = entry.second;
                if (existing.endpoint == endpoint && existing.client_nonce == nonce) {
                    // HELLO 重试绝不重置 next_id 或缓存，也不延长旧会话寿命。
                    return {"WELCOME:" + nonce + ":" + entry.first, false};
                }
            }
            if (sessions.size() >= MAX_SESSIONS) return {"BUSY:" + nonce, false};
            std::string token;
            do { token = new_token(); } while (sessions.count(token));
            Session session;
            session.endpoint = endpoint;
            session.client_nonce = nonce;
            session.last_activity = now;
            session.gap_since = now;
            sessions.emplace(token, std::move(session));
            std::cout << "[Session opened] " << token << " from " << endpoint << '\n';
            return {"WELCOME:" + nonce + ":" + token, false};
        }

        if (packet.compare(0, 6, "CLOSE:") == 0) {
            const std::string token = packet.substr(6);
            if (!valid_token(token)) return {};
            auto it = sessions.find(token);
            if (it != sessions.end() && it->second.endpoint == endpoint) {
                sessions.erase(it);
                std::cout << "[Session closed] " << token << '\n';
            }
            // CLOSE 为尽力通知，客户端不等待回复；重复 CLOSE 也不会创建会话。
            return {};
        }

        // DATA:<32位Session ID>:<Message ID>:<正文>
        if (packet.compare(0, 5, "DATA:") != 0 || packet.size() < 38
            || packet[37] != ':') return {};
        const std::string token = packet.substr(5, 32);
        if (!valid_token(token)) return {};
        auto it = sessions.find(token);
        if (it == sessions.end() || it->second.endpoint != endpoint) {
            // DATA 永远不能隐式创建会话。关闭/过期/重启后的旧数据会走这里。
            return {"ERROR:" + token + ":UNKNOWN_SESSION", false};
        }

        Session& session = it->second;
        std::uint32_t id = 0;
        std::string content;
        if (!parse_message(packet.data() + 38, packet.size() - 38, id, content)
            || id == 0 || content.size() > MAX_CONTENT_BYTES) {
            return {"ERROR:" + token + ":BAD_MESSAGE", false};
        }

        if (id < session.next_id) {
            // ACK 只确认会话内的 ID，不把重传携带的正文当成新业务执行。
            session.last_activity = now;
            std::cout << "[Duplicate delivered] " << token << " ID=" << id << '\n';
            return {"ACK:" + token + ":" + std::to_string(id) + ":", true};
        }

        if (id == session.next_id) {
            deliver(token, id, content);
            ++session.next_id;
            while (session.next_id <= std::numeric_limits<std::uint32_t>::max()) {
                auto pending = session.pending.find(static_cast<std::uint32_t>(session.next_id));
                if (pending == session.pending.end()) break;
                deliver(token, pending->first, pending->second);
                session.pending_bytes -= pending->second.size();
                session.pending.erase(pending);
                ++session.next_id;
            }
            session.last_activity = now;
            session.gap_since = now; // 有序交付确实推进了，重新开始等待下一个缺口。
            return {"ACK:" + token + ":" + std::to_string(id) + ":", true};
        }

        auto duplicate = session.pending.find(id);
        if (duplicate != session.pending.end()) {
            if (duplicate->second != content) {
                // 身份冲突使整个会话失效，不能确认互相矛盾的两份正文。
                sessions.erase(it);
                return {"ERROR:" + token + ":ID_CONFLICT", false};
            }
            session.last_activity = now;
            // 重传不会重置 gap_since，避免缺口一直存在却永不过期。
            return {"ACK:" + token + ":" + std::to_string(id) + ":", true};
        }

        if (static_cast<std::uint64_t>(id) - session.next_id > RECEIVE_WINDOW
            || session.pending.size() >= MAX_PENDING_MESSAGES
            || content.size() > MAX_PENDING_BYTES - session.pending_bytes) {
            // 保留已经接纳的缓存，只拒绝新消息；客户端收到 ERROR 后终止会话。
            return {"ERROR:" + token + ":RECEIVE_LIMIT", false};
        }
        if (session.pending.empty()) session.gap_since = now;
        session.pending.emplace(id, content);
        session.pending_bytes += content.size();
        session.last_activity = now;
        std::cout << "[Buffered] " << token << " ID=" << id
                  << " waiting=" << session.next_id << '\n';
        return {"ACK:" + token + ":" + std::to_string(id) + ":", true};
    }

private:
    static void deliver(const std::string& token, std::uint32_t id,
                        const std::string& content)
    {
        // 业务处理入口：即时到达和缓存中的消息统一走这里。
        std::cout << "[Deliver] session=" << token << " ID=" << id << " text=";
        std::cout.write(content.data(), static_cast<std::streamsize>(content.size()));
        std::cout << '\n' << std::flush;
    }
};

// Linux socket loop. Above this line is platform-independent session logic.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>

int main()
{
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) { std::perror("socket"); return 1; }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        std::perror("bind"); close(sock); return 1;
    }
    // 周期性唤醒，确保空闲时也能回收会话和乱序缓存。
    const timeval timeout{1, 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        std::perror("setsockopt"); close(sock); return 1;
    }

    SessionServer server;
    std::uint64_t accepted_data_count = 0;
    std::cout << "UDP session server listening on 0.0.0.0:9000\n"
              << "Idle timeout: 60s; gap timeout: 30s. Ctrl+C to stop.\n" << std::flush;
    char buffer[65536];
    try {
        while (true) {
            server.expire_sessions(Clock::now());
            sockaddr_in client{};
            socklen_t client_size = sizeof(client);
            const ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                reinterpret_cast<sockaddr*>(&client), &client_size);
            if (received == -1) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                // 某个客户端关闭端口或网络不可达，不应停止整个服务器。
                if (errno == ECONNREFUSED || errno == ECONNRESET
                    || errno == EHOSTUNREACH || errno == ENETUNREACH) {
                    std::perror("peer network error");
                    continue;
                }
                std::perror("recvfrom"); close(sock); return 1;
            }
            char ip[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip))) continue;
            const std::string endpoint = std::string(ip) + ":" + std::to_string(ntohs(client.sin_port));
            const Reply reply = server.handle_packet(endpoint,
                std::string(buffer, static_cast<std::size_t>(received)), Clock::now());
            if (reply.packet.empty()) continue;
            if (reply.accepted_data && ++accepted_data_count % 3 == 0) {
                std::cout << "[Simulated loss] Skipping " << reply.packet << '\n' << std::flush;
                continue;
            }
            if (sendto(sock, reply.packet.data(), reply.packet.size(), 0,
                reinterpret_cast<const sockaddr*>(&client), client_size) == -1) {
                std::perror("sendto");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Server stopped: " << error.what() << '\n';
        close(sock);
        return 1;
    }
}

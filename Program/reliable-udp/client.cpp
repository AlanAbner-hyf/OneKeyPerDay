// Windows UDP client: explicit session, bounded retries, interactive input.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>

using Clock = std::chrono::steady_clock;
constexpr int MAX_ATTEMPTS = 3;
constexpr auto REPLY_TIMEOUT = std::chrono::seconds(5);
constexpr std::size_t MAX_CONTENT_BYTES = 65507 - 49;

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


// 返回 1=收到指定来源的数据，0=本轮截止时间已到，-1=socket 错误。
int receive_until(SOCKET sock, const sockaddr_in& server, Clock::time_point deadline,
                  std::string& packet)
{
    while (Clock::now() < deadline) {
        const auto now = Clock::now();
        if (now >= deadline) return 0;
        const DWORD remaining_ms = static_cast<DWORD>(
            std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count());
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&remaining_ms),
                       sizeof(remaining_ms)) == SOCKET_ERROR) {
            std::cerr << "setsockopt failed: " << WSAGetLastError() << '\n';
            return -1;
        }
        char buffer[65536];
        sockaddr_in sender{};
        int sender_size = sizeof(sender);
        const int received = recvfrom(sock, buffer, sizeof(buffer), 0,
            reinterpret_cast<sockaddr*>(&sender), &sender_size);
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) continue;
            std::cerr << "recvfrom failed: " << error << '\n';
            return -1;
        }
        if (sender.sin_family != AF_INET || sender.sin_port != server.sin_port
            || sender.sin_addr.s_addr != server.sin_addr.s_addr) continue;
        packet.assign(buffer, static_cast<std::size_t>(received));
        return 1;
    }
    return 0;
}

bool send_packet(SOCKET sock, const sockaddr_in& server, const std::string& packet)
{
    if (sendto(sock, packet.data(), static_cast<int>(packet.size()), 0,
               reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR) {
        std::cerr << "sendto failed: " << WSAGetLastError() << '\n';
        return false;
    }
    return true;
}

// 只有显式握手才能创建会话。每次客户端启动使用新的 nonce。
bool open_session(SOCKET sock, const sockaddr_in& server, std::string& session_id)
{
    const std::string nonce = new_token();
    const std::string hello = "HELLO:" + nonce;
    const std::string welcome_prefix = "WELCOME:" + nonce + ":";
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        std::cout << "[Open session] attempt " << attempt << '/' << MAX_ATTEMPTS << '\n';
        if (!send_packet(sock, server, hello)) return false;
        const auto deadline = Clock::now() + REPLY_TIMEOUT;
        while (true) {
            std::string packet;
            const int result = receive_until(sock, server, deadline, packet);
            if (result < 0) return false;
            if (result == 0) break;
            if (packet == "BUSY:" + nonce) {
                std::cerr << "Server session limit reached.\n";
                return false;
            }
            if (packet.compare(0, welcome_prefix.size(), welcome_prefix) == 0) {
                const std::string token = packet.substr(welcome_prefix.size());
                if (valid_token(token)) {
                    session_id = token;
                    std::cout << "[Session opened] " << session_id << '\n';
                    return true;
                }
            }
            // 旧启动的握手回复 nonce 不同，忽略且不重置截止时间。
        }
    }
    std::cerr << "Could not establish a session.\n";
    return false;
}

// CLOSE 是尽力通知；丢包、Ctrl+C、断网时由服务端的超时清理兜底。
void close_session(SOCKET sock, const sockaddr_in& server, const std::string& session_id)
{
    if (!session_id.empty()) {
        send_packet(sock, server, "CLOSE:" + session_id);
        std::cout << "[Session close requested] " << session_id << '\n';
    }
}

bool send_message_with_retry(SOCKET sock, const sockaddr_in& server,
                             const std::string& session_id, std::uint32_t id,
                             const std::string& content)
{
    const std::string packet = "DATA:" + session_id + ":" + std::to_string(id) + ":" + content;
    const std::string ack_prefix = "ACK:" + session_id + ":";
    const std::string error_prefix = "ERROR:" + session_id + ":";
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        std::cout << "Message " << id << ", attempt " << attempt << '/' << MAX_ATTEMPTS << '\n';
        if (!send_packet(sock, server, packet)) return false;
        const auto deadline = Clock::now() + REPLY_TIMEOUT;
        while (true) {
            std::string reply;
            const int result = receive_until(sock, server, deadline, reply);
            if (result < 0) return false;
            if (result == 0) break;
            if (reply.compare(0, error_prefix.size(), error_prefix) == 0) {
                std::cerr << "Session rejected: " << reply.substr(error_prefix.size()) << '\n';
                return false; // 不自动重连重发，以免业务跨会话重复执行。
            }
            if (reply.compare(0, ack_prefix.size(), ack_prefix) != 0) continue;
            std::uint32_t ack_id = 0;
            std::string ack_content;
            if (!parse_message(reply.data() + ack_prefix.size(), reply.size() - ack_prefix.size(),
                               ack_id, ack_content) || !ack_content.empty()) continue;
            if (ack_id != id) {
                std::cout << "Ignoring old/unexpected ACK " << ack_id << '\n';
                continue;
            }
            // ACK 表示服务器已接纳这个身份的消息，乱序消息可能仍在等待交付。
            std::cout << "ACK OK. Message " << id << '\n' << std::flush;
            return true;
        }
        std::cout << "Timed out waiting for message " << id << ".\n";
    }
    std::cerr << "Message " << id << " was not confirmed after 3 attempts.\n";
    return false;
}

int run_client(SOCKET sock, const char* server_ip, bool test_order)
{
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(9000);
    if (inet_pton(AF_INET, server_ip, &server.sin_addr) != 1) {
        std::cerr << "Please provide a valid IPv4 address.\n";
        return 1;
    }
    std::string session_id;
    if (!open_session(sock, server, session_id)) return 1;

    if (test_order) {
        // 固定顺序实验；相同 ID 始终带相同正文。
        for (std::uint32_t id : {3u, 1u, 2u, 2u}) {
            if (!send_message_with_retry(sock, server, session_id, id,
                                          "test-" + std::to_string(id))) {
                close_session(sock, server, session_id);
                return 1;
            }
        }
        close_session(sock, server, session_id);
        return 0;
    }

    std::cout << "Enter a message and press Enter. /quit closes the session; Ctrl+C stops.\n"
              << "An idle session expires after 60 seconds; restart the client to open a new one.\n";
    std::uint32_t message_id = 0;
    std::string content;
    int exit_code = 0;
    while (true) {
        std::cout << "\nMessage> " << std::flush;
        if (!std::getline(std::cin, content) || content == "/quit") break;
        if (message_id == std::numeric_limits<std::uint32_t>::max()) {
            std::cerr << "Message IDs exhausted; closing this session.\n";
            exit_code = 1;
            break;
        }
        const std::uint32_t candidate = message_id + 1;
        const std::size_t packet_size = 5 + session_id.size() + 1
                                     + std::to_string(candidate).size() + 1 + content.size();
        if (content.size() > MAX_CONTENT_BYTES || packet_size > 65507) {
            std::cerr << "Message too long (maximum content " << MAX_CONTENT_BYTES << " bytes).\n";
            continue; // 不占用 ID，不留下永久缺口。
        }
        message_id = candidate;
        if (!send_message_with_retry(sock, server, session_id, message_id, content)) {
            std::cerr << "Stopping this ordered session; no later messages will be sent.\n";
            exit_code = 1;
            break;
        }
    }
    close_session(sock, server, session_id);
    return exit_code;
}

int main(int argc, char* argv[])
{
    const bool test_order = argc == 3 && std::string(argv[2]) == "--test-order";
    if (argc != 2 && !test_order) {
        std::cerr << "Usage: " << argv[0] << " <server_ipv4> [--test-order]\n";
        return 1;
    }
    WSADATA data{};
    const int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error != 0) { std::cerr << "WSAStartup failed: " << error << '\n'; return 1; }
    const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }
    int result = 1;
    try {
        result = run_client(sock, argv[1], test_order);
    } catch (const std::exception& exception) {
        std::cerr << "Client stopped: " << exception.what() << '\n';
    }
    closesocket(sock);
    WSACleanup();
    return result;
}

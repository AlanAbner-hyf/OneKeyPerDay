// Windows IPv4 UDP echo client with bounded timeout retries.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <chrono>

#include <cstddef>
#include <limits>

// 解析“十进制 Message ID:正文”。成功返回 true 并填写输出参数；
// 失败返回 false，输出参数保持不变。缓冲区不需要以 '\0' 结尾。
bool parse_message(const char* buffer, std::size_t data_size,
                          std::uint32_t& message_id, std::string& message_content)
{
    if (buffer == nullptr) {
        return false;
    }

    std::size_t pos = 0;
    std::uint32_t parsed_id = 0;
    constexpr std::uint32_t max_id = std::numeric_limits<std::uint32_t>::max();

    // 只在实际收到的字节范围内寻找第一个冒号。
    while (pos < data_size && buffer[pos] != ':') {
        const char ch = buffer[pos];
        if (ch < '0' || ch > '9') {
            return false;
        }

        const std::uint32_t digit = static_cast<std::uint32_t>(ch - '0');
        if (parsed_id > (max_id - digit) / 10) {
            return false;
        }

        parsed_id = parsed_id * 10 + digit;
        ++pos;
    }

    // 拒绝空 ID 和缺少冒号的数据报。
    if (pos == 0 || pos == data_size) {
        return false;
    }

    // 正文允许为空，也允许包含冒号和空字节。
    message_content.assign(buffer + pos + 1, data_size - pos - 1);
    message_id = parsed_id;
    return true;
}

// 发送消息给服务器，并等待服务器的回显确认，最多重试两次
int echo_with_retry(SOCKET sock, const char* server_ip,
                    const sockaddr_in& server_addr, const uint32_t message_id, const std::string& message_content)
{
    std::string message = std::to_string(message_id) + ":" + message_content;
    const int message_size = static_cast<int>(message.size());

    // 同一个业务消息最多尝试发3次
    const int max_attempts = 3;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        // 每次尝试复用相同的socket和消息
        // 第一次发送自动分配本地地址和端口
        const int sent = sendto(
            sock, message.data(), message_size, 0,
            reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
        if (sent == SOCKET_ERROR) {
            std::cerr << "sendto failed: " << WSAGetLastError() << '\n';
            return 1;
        }
        // 本轮发送只有一个截止时间；收到旧回复不能重新计时。
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(5);
        std::cout << "Attempt " << attempt << '/' << max_attempts
                  << ": sent " << sent << " bytes to " << server_ip
                  << ":9000: " << message << '\n'
                  << "Waiting for echo (up to 5 seconds)...\n" << std::flush;

        // 内层只负责接收；只有本轮超时，才进入外层的下一次发送。
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }

            // 向上取整，避免剩余不足 1 毫秒时变成 0（无限等待）。
            const DWORD remaining_ms = static_cast<DWORD>(
                std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count());
            if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&remaining_ms),
                           sizeof(remaining_ms)) == SOCKET_ERROR) {
                std::cerr << "setsockopt failed: " << WSAGetLastError() << '\n';
                return 1;
            }

            // 用来接受服务器回显的缓冲区，最大为65536字节
            char buffer[65536];
            // 地址，表示我们收到的消息实际上是谁发出的
            sockaddr_in sender_addr{};
            int sender_addr_len = sizeof(sender_addr);
            // 只等待本轮剩余时间，每次接收前重新初始化来源地址长度。
            const int received = recvfrom(
                sock, buffer, sizeof(buffer), 0,
                reinterpret_cast<sockaddr*>(&sender_addr), &sender_addr_len);
            if (received == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error == WSAETIMEDOUT) {
                    // 返回循环顶部，用单调时钟检查是否已到截止时间。
                    continue;
                }
                // 非超时的 socket 错误仍报告给调用方。
                std::cerr << "recvfrom failed: " << error << '\n';
                return 1;
            }

            // 实际上收到的消息可能来自其他地址或端口，必须检查是否与我们发送的服务器地址一致
            if (sender_addr.sin_family != AF_INET ||
                sender_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr ||
                sender_addr.sin_port != server_addr.sin_port) {
                std::cerr << "Ignoring a datagram from an unexpected sender.\n";
                continue;
            }

            // 从buffer中提取消息ID和消息内容
            std::uint32_t received_message_id = 0;
            std::string received_message_content;
            if (!parse_message(buffer, static_cast<std::size_t>(received),
                               received_message_id, received_message_content)) {
                std::cerr << "Ignoring invalid message: expected uint32 ID followed by ':'.\n";
                continue;
            }

            // 先匹配 ID；旧消息的迟到回复不消耗重传次数。
            if (received_message_id != message_id) {
                std::cerr << "Ignoring reply for message ID " << received_message_id
                          << "; waiting for ID " << message_id << ".\n";
                continue;
            }

            if (received_message_content == message_content) {
                std::cout << "Echo from " << server_ip << ":9000: ";
                std::cout.write(buffer, received);
                std::cout << "\nEcho OK.\n";
                return 0;
            }
            else {
                std::cerr << "Ignoring reply with unexpected content for ID "
                          << message_id << ".\n";
            }
        }

        std::cout << "Timed out: no matching echo received within 5 seconds.\n";
        if (attempt < max_attempts) {
            std::cout << "Retrying the same message...\n";
        }
    }
    std::cerr << "No echo after " << max_attempts
              << " attempts; giving up.\n";
    return 1;
}

int run_client(SOCKET sock, const char* server_ip)
{
    // 服务器地址，表示我们希望发给谁
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        std::cerr << "Please provide a valid IPv4 address.\n";
        return 1;
    }

    std::cout << "Enter a message and press Enter. Press Ctrl+C to stop.\n";

    // 定义Message ID，这里的Message代表的是业务消息
    static uint32_t message_id = 0;
    // 定义Message内容
    std::string message_content;
    // 外层循环，控制不断接受用户输入的消息
    while (true) {
        std::cout << "\nMessage> " << std::flush;

        // 读取用户的整行输入
        if (!std::getline(std::cin, message_content)) {
            std::cout << "\nInput ended.\n";
            return 0;
        }
        
        // Message ID 从1开始，每次发送给服务器都会自增1
        ++message_id;
        // IPv4 UDP payload 限制，不能超过 65507 bytes
        if (message_content.size()+std::to_string(message_id).size()+1 > 65507) {
            std::cerr << "Message too long (maximum 65507 bytes).\n";
            continue;
        }

        // 发送消息给服务器
        if (echo_with_retry(sock, server_ip, server_addr, message_id, message_content) != 0) {
            std::cout << "Message was not confirmed. You can enter another.\n";
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <server_ipv4>\n";
        return 1;
    }

    WSADATA wsa_data{};
    const int startup_error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_error != 0) {
        std::cerr << "WSAStartup failed: " << startup_error << '\n';
        return 1;
    }

    const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }

    const int result = run_client(sock, argv[1]);
    closesocket(sock);
    WSACleanup();
    return result;
}

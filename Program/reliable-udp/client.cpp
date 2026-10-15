// Windows IPv4 UDP echo client with bounded timeout retries.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <cstdint>
#include <cstring>
#include <iostream>

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
        std::cout << "Attempt " << attempt << '/' << max_attempts
                  << ": sent " << sent << " bytes to " << server_ip
                  << ":9000: " << message << '\n'
                  << "Waiting for echo (up to 5 seconds)...\n" << std::flush;

        // 用来接受服务器回显的缓冲区，最大为65536字节
        char buffer[65536];
        // 地址，表示我们收到的消息实际上是谁发出的
        sockaddr_in sender_addr{};
        int sender_addr_len = sizeof(sender_addr);
        // 接收服务器回显，最多等待5秒
        const int received = recvfrom(
            sock, buffer, sizeof(buffer), 0,
            reinterpret_cast<sockaddr*>(&sender_addr), &sender_addr_len);
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                std::cout << "Timed out: no echo received within 5 seconds.\n";
                // 如果没有收到回显，继续尝试发送同一条消息，最多重发2次
                if (attempt < max_attempts) {
                    std::cout << "Retrying the same message...\n";
                }
                continue;
            }
            // 如果重发3次都没有收到回显，直接返回错误
            std::cerr << "recvfrom failed: " << error << '\n';
            return 1;
        }

        // 实际上收到的消息可能来自其他地址或端口，必须检查是否与我们发送的服务器地址一致
        if (sender_addr.sin_family != AF_INET ||
            sender_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr ||
            sender_addr.sin_port != server_addr.sin_port) {
            std::cerr << "Received a datagram from an unexpected sender.\n";
            return 1;
        }

        // 从buffer中提取消息ID和消息内容
        const char* buffer_ptr = buffer;
        uint32_t received_message_id = 0;
        while (*buffer_ptr && *buffer_ptr != ':') {
            received_message_id = received_message_id * 10 + (*buffer_ptr - '0');
            ++buffer_ptr;
        }
        if (*buffer_ptr == ':') {
            ++buffer_ptr;
        }
        std::string received_message(buffer_ptr, received - (buffer_ptr - buffer));

        if (received != message_size) {
            std::cerr << "Reply length did not match the original message length.\n";
            return 1;
        }
        else if (received_message_id != message_id) {
            std::cerr << "Reply message ID did not match the original message ID.\n";
            return 1;
        }
        else if (received_message != message_content) {
            std::cerr << "Reply message content did not match the original message content.\n";
            return 1;
        }

        std::cout << "Echo from " << server_ip << ":9000: ";
        std::cout.write(buffer, received);
        std::cout << "\nEcho OK.\n";
        return 0;
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

    // Configure once; all input lines use the same socket and timeout.
    const DWORD timeout_ms = 5000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << '\n';
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

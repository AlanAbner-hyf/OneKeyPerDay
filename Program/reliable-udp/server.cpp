// Step 1: Linux IPv4 UDP echo server.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

#include <cstddef>
#include <limits>
#include <string>

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

int main()
{
    // AF_INET = IPv4; SOCK_DGRAM + IPPROTO_UDP = UDP.
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        std::perror("socket");
        return 1;
    }

    // 9000端口，接收来自任意IPv4地址的UDP数据报
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, reinterpret_cast<const sockaddr*>(&server_addr),
             sizeof(server_addr)) == -1) {
        std::perror("bind");
        close(sock);
        return 1;
    }

    std::cout << "UDP echo server listening on 0.0.0.0:9000\n"
              << "Press Ctrl+C to stop.\n" << std::flush;

    // 接收客户端消息
    char buffer[65536];
    // 服务端成功接受到的客户端消息数，不管消息是否合法
    std::uint64_t received_count = 0;
    // 定义一个保存处理过的客户端消息的集合
    std::unordered_map<std::string, std::unordered_set<uint32_t>> processed_messages;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);

        // Wait for one datagram and obtain its sender's address.
        const ssize_t received = recvfrom(
            sock, buffer, sizeof(buffer), 0,
            reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);

        if (received == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("recvfrom");
            close(sock);
            return 1;
        }

        ++received_count;

        char client_ip[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == nullptr) {
            std::perror("inet_ntop");
            continue;
        }

        // 在服务端打印接收到的客户端的消息
        std::cout << "Received " << received << " bytes from "
                  << client_ip << ':' << ntohs(client_addr.sin_port) << ": ";
        // Network data need not end in '\0': print exactly received bytes.
        std::cout.write(buffer, received);
        std::cout << '\n' << std::flush;

        // 从buffer中提取消息ID和消息内容
        std::uint32_t received_message_id = 0;
        std::string received_message_content;
        if (!parse_message(buffer, static_cast<std::size_t>(received),
                           received_message_id, received_message_content)) {
            std::cerr << "Invalid message: expected uint32 ID followed by ':'.\n";
            continue;  // 消息解析失败，进入服务端下一轮接收，不处理、不确认这条消息。
        }

        // 客户端的ip和port
        std::string client_key = std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
        // 已经处理过了
        if(processed_messages[client_key].count(received_message_id)) {
            std::cout << "Duplicate message received from " << client_key << '\n' << std::flush;
        }
        // 还没处理过
        else {
            std::cout << "Processing new message with ID: " << received_message_id << " from " << client_key << '\n' << std::flush;
            processed_messages[client_key].insert(received_message_id);
        }

        // 模拟丢失确认消息 3, 6, 9, ...
        if (received_count % 3 == 0) {
            std::cout << "[Simulated loss] Skipping reply for message #"
                      << received_count << '\n' << std::flush;
            continue;
        }

        // 给客户端发送确认消息
        const ssize_t sent = sendto(
            sock, buffer, static_cast<size_t>(received), 0,
            reinterpret_cast<const sockaddr*>(&client_addr), client_addr_len);

        if (sent == -1) {
            std::perror("sendto");
        }
    }
}

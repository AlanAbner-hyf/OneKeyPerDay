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

int main()
{
    // AF_INET = IPv4; SOCK_DGRAM + IPPROTO_UDP = UDP.
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        std::perror("socket");
        return 1;
    }

    // Listen on port 9000 on all local IPv4 interfaces.
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

    // Large enough for any IPv4 UDP payload; start with short text messages.
    char buffer[65536];
    // Counts all successfully received datagrams until the server restarts.
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

        std::cout << "Received " << received << " bytes from "
                  << client_ip << ':' << ntohs(client_addr.sin_port) << ": ";
        // Network data need not end in '\0': print exactly received bytes.
        std::cout.write(buffer, received);
        std::cout << '\n' << std::flush;

        // Simulate a lost reply for datagrams 3, 6, 9, ...
        if (received_count % 3 == 0) {
            std::cout << "[Simulated loss] Skipping reply for message #"
                      << received_count << '\n' << std::flush;
            continue;
        }

        // Reply to the observed sender, using the exact datagram length.
        const ssize_t sent = sendto(
            sock, buffer, static_cast<size_t>(received), 0,
            reinterpret_cast<const sockaddr*>(&client_addr), client_addr_len);

        if (sent == -1) {
            std::perror("sendto");
        }
        
        // 因为我希望不论是否是重复的消息都要回复，所以我把回复放在了前面
        // 客户端的ip和port
        std::string client_key = std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
        // 解析消息中的Message ID
        uint32_t message_id = 0;
        // received要大于等于message_id的大小
        if (received >= static_cast<ssize_t>(sizeof(message_id))) {
            std::memcpy(&message_id, buffer, sizeof(message_id));
            message_id = ntohl(message_id); // Convert from network byte order to host byte order
        }
        // 已经处理过了
        if(processed_messages[client_key].count(message_id)) {
            std::cout << "Duplicate message received from " << client_key << '\n' << std::flush;
            continue;
        }
        // 还没处理过
        else {
            std::cout << "Processing new message with ID: " << message_id << " from " << client_key << '\n' << std::flush;
            processed_messages[client_key].insert(message_id);
        }
    }
}

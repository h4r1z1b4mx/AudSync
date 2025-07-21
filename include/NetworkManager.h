#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>

enum class MessageType : uint8_t {
    CONNECT = 1,
    DISCONNECT = 2,
    AUDIO_DATA = 3,
    HEARTBEAT = 4,
    CLIENT_READY = 5
};

struct Message {
    MessageType type;
    uint32_t size;
    std::vector<uint8_t> data;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Client methods
    bool connectToServer(const std::string& host, int port);
    void disconnect();
    
    // Server methods
    bool startServer(int port);
    void stopServer();
    
    // Common methods
    bool sendMessage(const Message& message, int socket_fd = -1);
    bool receiveMessage(Message& message, int socket_fd = -1);
    
    void setMessageHandler(std::function<void(const Message&, int)> handler);
    bool isConnected() const;
    
    // Get client socket for server mode
    int getClientSocket() const { return client_socket_; }

private:
    int server_socket_;
    int client_socket_;
    std::atomic<bool> is_server_;
    std::atomic<bool> running_;
    
    std::thread accept_thread_;
    std::function<void(const Message&, int)> message_handler_;
    
    void acceptClients();
    void handleClient(int client_fd);
    bool sendRaw(const void* data, size_t size, int socket_fd);
    bool receiveRaw(void* data, size_t size, int socket_fd);
};

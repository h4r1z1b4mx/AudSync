#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

enum class MessageType : uint32_t {
    CONNECT = 1,
    DISCONNECT = 2,
    AUDIO_DATA = 3,
    CLIENT_CONFIG = 4,
    CLIENT_READY = 5,
    HEARTBEAT = 6
};

struct Message {
    MessageType type;
    uint32_t size;
    uint64_t timestamp;
    std::vector<uint8_t> data;
    
    Message() : type(MessageType::CONNECT), size(0), timestamp(0) {}
};

class NetworkManager {
public:
    using MessageHandler = std::function<void(const Message&, SOCKET)>;

    NetworkManager();
    ~NetworkManager();

    // Server methods
    bool startServer(int port);
    void stopServer();
    bool sendMessage(const Message& message, SOCKET clientSocket);
    
    // Client methods
    bool connectToServer(const std::string& host, int port);
    void disconnect();
    bool sendMessage(const Message& message);
    bool receiveMessage(Message& message);
    bool receiveMessage(Message& message, SOCKET socket);  // ← ADD THIS LINE
    
    // Common methods
    void setMessageHandler(MessageHandler handler);
    bool isConnected() const;

private:
    // Network state
    SOCKET serverSocket_;
    SOCKET clientSocket_;
    std::atomic<bool> isServer_;
    std::atomic<bool> isConnected_;
    std::atomic<bool> running_;
    
    // Threading
    std::thread serverThread_;
    std::thread clientHandlerThread_;
    std::mutex clientsMutex_;
    
    // Message handling
    MessageHandler messageHandler_;
    std::vector<SOCKET> connectedClients_;
    
    // Internal methods
    void serverLoop();
    void handleClient(SOCKET clientSocket);
    bool initializeWinsock();
    void cleanupWinsock();
    bool sendRawData(SOCKET socket, const void* data, size_t size);
    bool receiveRawData(SOCKET socket, void* data, size_t size);
    void closeSocket(SOCKET& socket);
    
    // Message serialization
    bool serializeMessage(const Message& message, std::vector<uint8_t>& buffer);
    bool deserializeMessage(const std::vector<uint8_t>& buffer, Message& message);
};
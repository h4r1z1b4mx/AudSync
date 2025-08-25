#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <algorithm>
#include <atomic>
#include <string>
#include "Message.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

class AudioServer {
private:
    int server_socket_;
    std::vector<int> client_sockets_;
    std::mutex clients_mutex_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;

public:
    AudioServer() : server_socket_(-1), running_(false) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~AudioServer() {
        stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool start(int port) {
        // Create server socket
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ < 0) {
            std::cerr << "Failed to create server socket" << std::endl;
            return false;
        }

        // Enable address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        // Bind socket
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(static_cast<u_short>(port));

        if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to bind socket to port " << port << std::endl;
            close(server_socket_);
            return false;
        }

        // Listen for connections
        if (listen(server_socket_, 10) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            close(server_socket_);
            return false;
        }

        running_ = true;
        accept_thread_ = std::thread(&AudioServer::acceptClients, this);

        std::cout << "🚀 Audio Server started on port " << port << std::endl;
        std::cout << "📡 Waiting for clients to connect..." << std::endl;
        return true;
    }

    void stop() {
        running_ = false;
        
        // Close all client sockets
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (int socket : client_sockets_) {
                close(socket);
            }
            client_sockets_.clear();
        }

        // Close server socket
        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }

        // Wait for threads
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();

        std::cout << "🛑 Audio Server stopped" << std::endl;
    }

    void printStatus() {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        std::cout << "📊 Server Status: " << client_sockets_.size() << " clients connected" << std::endl;
    }

private:
    void acceptClients() {
        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                if (running_) {
                    std::cerr << "Failed to accept client connection" << std::endl;
                }
                continue;
            }

#ifdef _WIN32
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::cout << "✅ Client connected from " << client_ip 
                      << ":" << ntohs(client_addr.sin_port) << std::endl;
#else
            std::cout << "✅ Client connected from " << inet_ntoa(client_addr.sin_addr) 
                      << ":" << ntohs(client_addr.sin_port) << std::endl;
#endif

            // Add client to list
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                client_sockets_.push_back(client_socket);
            }

            // Start client handler thread
            client_threads_.emplace_back(&AudioServer::handleClient, this, client_socket);
            
            printStatus();
        }
    }

    void handleClient(int client_socket) {
        std::cout << "🔄 Started handling client on socket " << client_socket << std::endl;
        
        while (running_) {
            // Try to receive a complete message
            MessageHeader header;
            
            // Receive message header
            int received = recv(client_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);
            if (received <= 0) break;
            
            // Validate magic number
            if (header.magic != 0x41554453) {
                std::cerr << "Invalid magic number from client" << std::endl;
                break;
            }
            
            // Calculate data size
            uint32_t dataSize = header.length - sizeof(MessageHeader);
            
            // Create message and set header
            Message message(header.type);
            message.setSequence(header.sequence);
            message.setTimestamp(header.timestamp);
            
            // Receive data if present
            if (dataSize > 0) {
                std::vector<uint8_t> buffer(dataSize);
                received = recv(client_socket, reinterpret_cast<char*>(buffer.data()), dataSize, 0);
                if (received <= 0) break;
                
                message.setData(buffer.data(), dataSize);
            }

            // Relay audio data to all other clients
            if (message.getType() == MessageType::AUDIO_DATA) {
                relayAudioToOtherClients(client_socket, message);
            } else if (message.getType() == MessageType::HEARTBEAT) {
                // Send heartbeat response back to sender
                sendMessage(client_socket, message);
            }
        }

        // Remove client from list
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = std::find(client_sockets_.begin(), client_sockets_.end(), client_socket);
            if (it != client_sockets_.end()) {
                client_sockets_.erase(it);
            }
        }

        close(client_socket);
        std::cout << "❌ Client disconnected from socket " << client_socket << std::endl;
        printStatus();
    }

    void relayAudioToOtherClients(int sender_socket, const Message& message) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        int relayed_count = 0;
        for (int client_socket : client_sockets_) {
            if (client_socket != sender_socket) {  // Don't echo back to sender
                if (sendMessage(client_socket, message)) {
                    relayed_count++;
                }
            }
        }
        
        // Optional: Print relay stats every 100 messages
        static int message_count = 0;
        if (++message_count % 100 == 0) {
            std::cout << "📡 Relayed " << message_count << " audio packets to " << relayed_count << " clients" << std::endl;
        }
    }

    bool sendMessage(int client_socket, const Message& message) {
        // Serialize the message
        std::vector<uint8_t> serialized = message.serialize();
        
        // Send the complete serialized message
        int sent = send(client_socket, reinterpret_cast<const char*>(serialized.data()), serialized.size(), 0);
        return sent == static_cast<int>(serialized.size());
    }
};

int main(int argc, char* argv[]) {
    int port = 12345;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    std::cout << "🎵 AudSync Audio Server" << std::endl;
    std::cout << "========================" << std::endl;

    AudioServer server;
    
    if (!server.start(port)) {
        std::cerr << "❌ Failed to start server on port " << port << std::endl;
        return 1;
    }

    std::cout << "\n🎵 Audio Server Running!" << std::endl;
    std::cout << "📋 Instructions:" << std::endl;
    std::cout << "   1. Run first client:  .\\audsync_client.exe" << std::endl;
    std::cout << "   2. Run second client: .\\audsync_client.exe" << std::endl;
    std::cout << "   3. In each client:" << std::endl;
    std::cout << "      - Type: connect localhost " << port << std::endl;
    std::cout << "      - Type: start" << std::endl;
    std::cout << "   4. Speak into one - hear on the other!" << std::endl;
    std::cout << "\nPress Enter to stop server..." << std::endl;

    // Wait for Enter key
    std::string input;
    std::getline(std::cin, input);

    server.stop();
    return 0;
}
#pragma once

#include "NetworkManager.h"
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

struct ClientInfo {
  SOCKET socket_fd;
  bool ready;
  std::string id;
};

class AudioServer {
  public:
    AudioServer();
    ~AudioServer();

    bool start(int port);
    void stop();

    bool isRunning() const;
    size_t getConnectedClients() const;
  
  private:
    NetworkManager network_manager_;
    std::vector<ClientInfo> clients_;
    std::atomic<bool> running_;

    mutable std::mutex clients_mutex;
    std::thread server_thread_;

    void handleClientMessage(const Message& message, SOCKET client_socket);
    void broadcastAudioToOthers(const Message& message, SOCKET sender_socket);
    void addClient(SOCKET socket_fd);
    void removeClient(SOCKET socket_fd);
    void serverLoop();
};

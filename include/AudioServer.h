#pragma once

#include "NetworkManager.h"
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

struct ClientInfo {
  int socket_fd;
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

    void handleClientMessage(const Message& message, int client_socket);
    void broadcastAudioToOthers(const Message& message, int sender_socket);
    void addClient(int socket_fd);
    void removeClient(int socket_fd);
    void serverLoop();
};

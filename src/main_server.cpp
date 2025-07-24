#include "AudioServer.h"
#include <iostream>
#include <signal.h>

AudioServer* g_server = nullptr;

void signalHandler(int signal) {
  (void) signal;
  if(g_server){
    std::cout<<"\nShutting down server... " << std::endl;
    g_server->stop();
  }
}

int main(int argc, char* argv[]) {
  int port = 8080;
  if (argc >= 2) {
    port = std::stoi(argv[1]);
  }

  std::cout << "AudSync Server - Real-time Audio Streaming Hub" <<endl;
  std::cout << "Starting server on port: "<<port << std::endl;
  
  AudioServer server;
  g_server = &server;

  // Set up signal handler for graceful shutdown
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  
  if (!server.start(port)) {
    std::cerr << "Failed to start server on port "<< port << std::endl;
    return 1;
  }

  std::cout<< "Server is running. Press Ctrl + C to stop" << std::endl;
  std::cout<< "Client can connect using: ./audsync_client <server_ip> " <<port<< std::endl;
  
  std::string command;
  while (server.isRunning() && std::cin >> command) {
    if (command == "quit" || command == "stop") {
        break;
    } else if (command == "status") {
        std::cout << "Connected clients: " << server.getConnectedClients() << std::endl;
    } else if (command == "help") {
        std::cout << "Commands:" << std::endl;
        std::cout << "  status - Show server status" << std::endl;
        std::cout << "  quit   - Stop server and exit" << std::endl;
        std::cout << "  help   - Show this help" << std::endl;
    } else {
        std::cout << "Unknown command. Type 'help' for available commands." << std::endl;
     }
  }
  std::cout << "Server shutting down... " << std::endl;
  return 0;
}



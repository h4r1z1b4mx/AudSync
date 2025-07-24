#include "AudioClient.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  std::string server_host = "127.0.0.1";
  int server_port = 8080;

  //Pase Command line arguments
  if (argc >= 2) {
    server_host = argv[1];
  }

  if (argc >= 3) {
    server_port = std::stoi(argv[2]);
  }

  std::cout << "AudSync Client - Real-time Audio Streaming" << std::endl;
  std::cout << "Connecting to Server: " << server_host << " : "<< std::endl;
  
  AudioClient client;

  if(!client.connect(server_host, server_port)){
    std::cerr << "Failed to connect to server. Make sure the server is running. "<< std::endl;
    return 1;
  }

  std::cout<< "Successfully connected to Server! "<< std::endl;
  std::cout<< "Type 'start' to begin audio streaming, 'stop' to pause, 'quit' to exit. "<<std::endl;
  
  //Run the client main loop
  client.run();

  std::cout<<"Client shutting down... " << std::endl;
  return 0;

}

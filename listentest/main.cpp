#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#include "networkthread.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ovsocket.lib")
using namespace ovs;

#define MAX_PACKET_LENGTH 4096

int main(int argc, char **argv)
{
   WSADATA wsaData;
   WSAStartup(MAKEWORD(2, 2), &wsaData);
   NetworkThread thread;
   Socket *socket = new Socket();

   // On socket error
   socket->addErrorListener([](Socket *socket, int code){
      std::cout << "Listen Socket Error: " << code << std::endl;
   });

   socket->addDisconnectListener([](Socket *socket){
      std::cout << "Listen Socket Disconnected" << std::endl;
   });

   // On socket connected, accept pls
   socket->addAcceptListener([&thread](Socket *socket){
      auto newSock = socket->accept();

      if (!newSock) {
         std::cout << "Failed to accept new connection" << std::endl;
         return;
      } else {
         std::cout << "New Connection Accepted" << std::endl;
      }

      newSock->addErrorListener([](Socket *socket, int code){
         std::cout << "Socket Error: " << code << std::endl;
      });

      newSock->addDisconnectListener([](Socket *socket){
         std::cout << "Socket Disconnected" << std::endl;
      });

      newSock->addReceiveListener([](Socket *socket, const char *buffer, size_t size){
         std::cout << "Receive size " << size << ": " << buffer << std::endl;
         socket->recvPartial(MAX_PACKET_LENGTH);
      });

      newSock->recvPartial(MAX_PACKET_LENGTH);
      thread.addSocket(newSock);
   });

   if (!socket->listen("127.0.0.1", "23946")) {
      std::cout << "Error starting connect!" << std::endl;
      return 0;
   }

   thread.addSocket(socket);
   thread.start();
   WSACleanup();
   return 0;
}

#include "networkthread.h"
#include <algorithm>

namespace ovs
{

NetworkThread::NetworkThread()
{
   mInternalEvent = WSACreateEvent();
}

NetworkThread::~NetworkThread()
{
}

bool NetworkThread::addSocket(Socket *socket)
{
   socket->addDisconnectListener(std::bind(&NetworkThread::removeSocket, this, std::placeholders::_1));
   mSocketAddQueue.push(socket);
   triggerInternalEvent();
   return true;
}

void NetworkThread::removeSocket(Socket *socket)
{
   mSocketRemoveQueue.push(socket);
   triggerInternalEvent();
}

void NetworkThread::start()
{
   mRunning = true;
   run();
}

void NetworkThread::stop()
{
   mRunning = false;
   triggerInternalEvent();
}

void NetworkThread::triggerInternalEvent()
{
   WSASetEvent(mInternalEvent);
}

bool NetworkThread::run()
{
   std::vector<WSAEVENT> events;
   std::vector<Socket*> sockets;

   while (mRunning) {
      events.clear();
      events.push_back(mInternalEvent);

      for (auto socket : sockets) {
         events.push_back(socket->mEvent);
      }

      auto result = WSAWaitForMultipleEvents(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE, TRUE);

      if (result == WSA_WAIT_IO_COMPLETION) {
         continue;
      }

      if (result == WSA_WAIT_TIMEOUT) {
         continue;
      }

      if (result == WSA_WAIT_FAILED) {
         break;
      }

      if (result >= WSA_WAIT_EVENT_0 && result < WSA_WAIT_EVENT_0 + events.size()) {
         auto index = result - WSA_WAIT_EVENT_0;
         auto event = events[index];
         WSAResetEvent(event);

         if (event == mInternalEvent) {
            while (auto socket = mSocketAddQueue.pop()) {
               sockets.push_back(socket);
            }

            while (auto socket = mSocketRemoveQueue.pop()) {
               sockets.erase(std::remove(sockets.begin(), sockets.end(), socket), sockets.end());
               delete socket;
            }
         } else {
            auto socket = sockets[index - 1];
            socket->checkRequests();
         }
      }
   }

   return false;
}

}

#pragma once
#include "socket.h"

namespace ovs
{

class NetworkThread
{
public:
   NetworkThread();
   ~NetworkThread();

   bool addSocket(Socket *socket);
   void removeSocket(Socket *socket);

   void start();
   void stop();

private:
   void triggerInternalEvent();
   bool run();

private:
   volatile bool mRunning;
   WSAEVENT mInternalEvent;
   LocklessQueue<Socket*> mSocketAddQueue;
   LocklessQueue<Socket*> mSocketRemoveQueue;
};

}

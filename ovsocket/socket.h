#pragma once
#include <functional>
#include <string>
#include <vector>
#include <WS2tcpip.h>
#include <WinSock2.h>
#include "locklesspool.h"

namespace ovs
{

enum class RequestType
{
   Invalid,
   Send,
   ReceiveFill,
   ReceivePartial,
   Accept,
   Finished
};

struct Request : Queueable<Request>
{
   Request()
   {
      memset(&overlapped, 0, sizeof(OVERLAPPED));
      memset(&buffer, 0, sizeof(WSABUF));
   }

   OVERLAPPED overlapped;
   WSABUF buffer;
   RequestType type = RequestType::Invalid;
   std::size_t transferred = 0;
   std::vector<char> data;
};

extern LocklessPool<Request> requestPool;

class Socket : public Queueable<Socket>
{
public:
   using SendCallbackType = std::function<void(Socket *, const char *, std::size_t)>;
   using ReceiveCallbackType = std::function<void(Socket *, const char *, std::size_t)>;
   using ConnectCallbackType = std::function<void(Socket *)>;
   using AcceptCallbackType = std::function<void(Socket *)>;
   using DisconnectCallbackType = std::function<void(Socket *)>;
   using ErrorCallbackType = std::function<void(Socket *, int)>;

   Socket();
   Socket(SOCKET handle);
   ~Socket();

   bool connect(const std::string &ip, const std::string &port);
   bool listen(const std::string &ip, const std::string &port);
   bool disconnect();

   Socket *accept();
   bool send(const char *buffer, std::size_t size);
   bool recv(std::size_t size, RequestType type = RequestType::ReceiveFill);
   bool recvFill(std::size_t size);
   bool recvPartial(std::size_t size);

   bool addErrorListener(const ErrorCallbackType &callback);
   bool addAcceptListener(const AcceptCallbackType &callback);
   bool addConnectListener(const ConnectCallbackType &callback);
   bool addDisconnectListener(const DisconnectCallbackType &callback);
   bool addReceiveListener(const ReceiveCallbackType &callback);
   bool addSendListener(const SendCallbackType &callback);

private:
   bool sendRequest(Request *request);
   bool recvRequest(Request *request);

protected:
   friend class NetworkThread;
   void checkRequests();

private:
   Request *allocateRequest();
   void freeRequest(Request *request);

   void onDisconnected();
   void onConnected();
   void onAccept();
   void onError(int code);
   bool onSend(Request *request, DWORD bytes);
   bool onReceive(Request *request, DWORD bytes);

private:
   void destroySocket();

private:
   bool mConnected;
   SOCKET mSocket;
   WSAEVENT mEvent;
   std::vector<Request*> mRequests;
   LocklessQueue<Request*> mRequestQueue;

   struct
   {
      std::vector<AcceptCallbackType> onAccept;
      std::vector<ConnectCallbackType> onConnect;
      std::vector<DisconnectCallbackType> onDisconnect;
      std::vector<SendCallbackType> onSend;
      std::vector<ReceiveCallbackType> onReceive;
      std::vector<ErrorCallbackType> onError;
   } mEventListeners;
};

}

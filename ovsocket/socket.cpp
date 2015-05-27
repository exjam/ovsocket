#include "socket.h"

namespace ovs
{

// Default size of 32
LocklessPool<Request> requestPool = LocklessPool<Request> { 32 };

Socket::Socket() :
   mSocket(INVALID_SOCKET)
{
   mEvent = WSACreateEvent();
}

Socket::Socket(SOCKET handle) :
   mSocket(handle), mConnected(true)
{
   mEvent = WSACreateEvent();
}

Socket::~Socket()
{
   destroySocket();
   WSACloseEvent(mEvent);
}

void Socket::destroySocket()
{
   mConnected = false;

   if (mSocket != INVALID_SOCKET) {
      closesocket(mSocket);
      mSocket = INVALID_SOCKET;
   }

   for (auto request : mRequests) {
      freeRequest(request);
   }

   mRequests.clear();
}

Request *Socket::allocateRequest()
{
   return requestPool.allocate();
}

void Socket::freeRequest(Request *request)
{
   requestPool.free(request);
}

bool Socket::connect(const std::string &ip, const std::string &port)
{
   addrinfo *result = nullptr;
   addrinfo hints;
   memset(&hints, 0, sizeof(addrinfo));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;
   mConnected = false;

   if (getaddrinfo(ip.c_str(), port.c_str(), &hints, &result) != NO_ERROR) {
      return false;
   }

   for (auto ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
      u_long mode = 1;
      mSocket = WSASocket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);

      if (mSocket == INVALID_SOCKET) {
         continue;
      }

      WSAResetEvent(mEvent);
      WSAEventSelect(mSocket, mEvent, FD_CLOSE | FD_WRITE | FD_CONNECT);

      if (ioctlsocket(mSocket, FIONBIO, &mode) == NO_ERROR) {
         if (::connect(mSocket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == NO_ERROR) {
            break;
         }

         if (WSAGetLastError() == WSAEWOULDBLOCK) {
            break;
         }
      }

      closesocket(mSocket);
      mSocket = INVALID_SOCKET;
   }

   freeaddrinfo(result);

   if (mSocket == INVALID_SOCKET) {
      return false;
   }

   return true;
}

bool Socket::listen(const std::string &ip, const std::string &port)
{
   addrinfo *result = nullptr;
   addrinfo hints;
   memset(&hints, 0, sizeof(addrinfo));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;
   mConnected = false;

   if (getaddrinfo(ip.c_str(), port.c_str(), &hints, &result) != NO_ERROR) {
      return false;
   }

   for (auto ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
      u_long mode = 1;
      mSocket = WSASocket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);

      if (mSocket == INVALID_SOCKET) {
         continue;
      }

      WSAResetEvent(mEvent);
      WSAEventSelect(mSocket, mEvent, FD_CLOSE | FD_WRITE | FD_ACCEPT);

      if (ioctlsocket(mSocket, FIONBIO, &mode) == NO_ERROR) {
         if (::bind(mSocket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == NO_ERROR) {
            break;
         }
      }

      closesocket(mSocket);
      mSocket = INVALID_SOCKET;
   }

   freeaddrinfo(result);

   if (mSocket == INVALID_SOCKET) {
      return false;
   }

   if (::listen(mSocket, 5) != NO_ERROR) {
      closesocket(mSocket);
      mSocket = INVALID_SOCKET;
      return false;
   }

   return true;
}

bool Socket::disconnect()
{
   return shutdown(mSocket, SD_SEND) == NO_ERROR;
}

Socket *Socket::accept()
{
   SOCKADDR_IN sa;
   INT len = sizeof(SOCKADDR_IN);
   auto socket = WSAAccept(mSocket, reinterpret_cast<sockaddr*>(&sa), &len, NULL, NULL);

   if (socket) {
      return new Socket(socket);
   } else {
      return nullptr;
   }
}

bool Socket::send(const char *buffer, std::size_t size)
{
   // Allocate request
   auto request = allocateRequest();
   request->type = RequestType::Send;

   // Allocate send buffer and copy in data
   request->data.resize(size);
   memcpy(request->data.data(), buffer, size);
   request->buffer.buf = request->data.data();
   request->buffer.len = static_cast<ULONG>(request->data.size());

   // Perform WSASend
   if (!sendRequest(request)) {
      freeRequest(request);
      return false;
   }

   mRequestQueue.push(request);
   return true;
}

bool Socket::recv(std::size_t size, RequestType type)
{
   auto request = allocateRequest();
   request->type = type;
   assert(type == RequestType::ReceiveFill || type == RequestType::ReceivePartial);

   // Allocate a receive buffer
   request->data.resize(size);
   request->buffer.buf = request->data.data();
   request->buffer.len = static_cast<ULONG>(request->data.size());

   // Perform WSARecv
   if (!recvRequest(request)) {
      freeRequest(request);
      return false;
   }

   mRequestQueue.push(request);
   return true;
}

bool Socket::recvFill(std::size_t size)
{
   return recv(size, RequestType::ReceiveFill);
}

bool Socket::recvPartial(std::size_t size)
{
   return recv(size, RequestType::ReceivePartial);
}

// Event listeners
bool Socket::addErrorListener(const ErrorCallbackType &callback)
{
   mEventListeners.onError.push_back(callback);
   return true;
}

bool Socket::addAcceptListener(const AcceptCallbackType &callback)
{
   mEventListeners.onAccept.push_back(callback);
   return true;
}

bool Socket::addConnectListener(const ConnectCallbackType &callback)
{
   mEventListeners.onConnect.push_back(callback);
   return true;
}

bool Socket::addDisconnectListener(const DisconnectCallbackType &callback)
{
   mEventListeners.onDisconnect.push_back(callback);
   return true;
}

bool Socket::addReceiveListener(const ReceiveCallbackType &callback)
{
   mEventListeners.onReceive.push_back(callback);
   return true;
}

bool Socket::addSendListener(const SendCallbackType &callback)
{
   mEventListeners.onSend.push_back(callback);
   return true;
}

bool Socket::sendRequest(Request *request)
{
   memset(&request->overlapped, 0, sizeof(OVERLAPPED));
   request->overlapped.hEvent = mEvent;
   request->transferred = 0;

   if (WSASend(mSocket, &request->buffer, 1, nullptr, 0, &request->overlapped, nullptr) == SOCKET_ERROR) {
      if (WSAGetLastError() != WSA_IO_PENDING) {
         return false;
      }
   }

   return true;
}

bool Socket::recvRequest(Request *request)
{
   DWORD flags = 0;
   memset(&request->overlapped, 0, sizeof(OVERLAPPED));
   request->overlapped.hEvent = mEvent;
   request->transferred = 0;

   if (WSARecv(mSocket, &request->buffer, 1, nullptr, &flags, &request->overlapped, nullptr) == SOCKET_ERROR) {
      if (WSAGetLastError() != WSA_IO_PENDING) {
         return false;
      }
   }

   return true;
}

void Socket::checkRequests()
{
   WSANETWORKEVENTS events;
   WSAEnumNetworkEvents(mSocket, mEvent, &events);

   while (auto request = mRequestQueue.pop()) {
      mRequests.push_back(request);
   }

   if (events.lNetworkEvents & FD_CONNECT) {
      if (events.iErrorCode[FD_CONNECT_BIT]) {
         onError(events.iErrorCode[FD_CONNECT_BIT]);
      } else {
         onConnected();
      }
   }

   if (events.lNetworkEvents & FD_ACCEPT) {
      if (events.iErrorCode[FD_ACCEPT_BIT]) {
         onError(events.iErrorCode[FD_ACCEPT_BIT]);
      } else {
         onAccept();
      }
   }

   if (events.lNetworkEvents & FD_CLOSE) {
      onDisconnected();
   }

   for (auto request : mRequests) {
      DWORD bytes, flags;

      if (!WSAGetOverlappedResult(mSocket, &request->overlapped, &bytes, FALSE, &flags)) {
         auto error = WSAGetLastError();

         if (error != WSA_IO_INCOMPLETE) {
            onError(error);
         }
      } else {
         switch (request->type) {
         case RequestType::Send:
            onSend(request, bytes);
            break;
         case RequestType::ReceiveFill:
         case RequestType::ReceivePartial:
            onReceive(request, bytes);
            break;
         }
      }
   }

   for (auto itr = mRequests.begin(); itr != mRequests.end();) {
      auto request = *itr;

      if (request->type == RequestType::Finished) {
         freeRequest(request);
         itr = mRequests.erase(itr);
      } else {
         itr++;
      }
   }
}

void Socket::onDisconnected()
{
   destroySocket();

   for (auto &callback : mEventListeners.onDisconnect) {
      callback(this);
   }
}

void Socket::onConnected()
{
   mConnected = true;

   for (auto &callback : mEventListeners.onConnect) {
      callback(this);
   }
}

void Socket::onAccept()
{
   for (auto &callback : mEventListeners.onAccept) {
      callback(this);
   }
}

void Socket::onError(int code)
{
   for (auto &callback : mEventListeners.onError) {
      callback(this, code);
   }

   shutdown(mSocket, SD_BOTH);
   destroySocket();
}

bool Socket::onSend(Request *request, DWORD bytes)
{
   request->transferred += bytes;
   request->buffer.buf += bytes;
   request->buffer.len -= bytes;

   if (request->transferred < request->data.size()) {
      sendRequest(request);
      return false;
   }

   // Reset buffer info
   request->buffer.buf -= request->transferred;
   request->type = RequestType::Finished;

   for (auto &callback : mEventListeners.onSend) {
      callback(this, request->buffer.buf, request->transferred);
   }

   return true;
}

bool Socket::onReceive(Request *request, DWORD bytes)
{
   request->transferred += bytes;
   request->buffer.buf += bytes;
   request->buffer.len -= bytes;

   if (request->type == RequestType::ReceiveFill && request->buffer.len) {
      recvRequest(request);
      return false;
   }

   // Reset buffer info
   request->buffer.buf -= request->transferred;
   request->type = RequestType::Finished;

   for (auto &callback : mEventListeners.onReceive) {
      callback(this, request->buffer.buf, request->transferred);
   }

   return true;
}

};

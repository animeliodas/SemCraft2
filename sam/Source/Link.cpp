/* SemCraft 2 - WebSocket client to the Minecraft mod. See Link.h.
 * Adapted from universal-modder's MIT minecraft-gta5-passthrough example (gta/src/ws.cpp). */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <stdint.h>

#include "Link.h"

namespace link {

static std::string _strHost;
static int _iPort = 0;
static uintptr_t _socket = ~uintptr_t(0);
static std::atomic<bool> _bConnected(false);
static std::atomic<bool> _bStop(false);
static std::atomic<int> _iGeneration(0);
static std::mutex _mxSend;
static std::mutex _mxQueue;
static std::deque<std::string> _queue;
static std::thread _thread;
static uint32_t _ulMask = 0x9E3779B9u;
static bool _bStarted = false;

static bool Open(void)
{
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)_iPort);
  inet_pton(AF_INET, _strHost.c_str(), &addr.sin_addr);

  if (connect(s, (sockaddr *)&addr, sizeof(addr)) != 0) {
    closesocket(s);
    return false;
  }

  BOOL bNoDelay = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&bNoDelay, sizeof(bNoDelay));

  const std::string strRequest = "GET / HTTP/1.1\r\nHost: " + _strHost + ":" + std::to_string(_iPort)
    + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

  if (::send(s, strRequest.data(), (int)strRequest.size(), 0) != (int)strRequest.size()) {
    closesocket(s);
    return false;
  }

  std::string strResponse;
  char c;

  while (strResponse.size() < 4096 && strResponse.find("\r\n\r\n") == std::string::npos) {
    if (recv(s, &c, 1, 0) != 1) {
      closesocket(s);
      return false;
    }
    strResponse += c;
  }

  if (strResponse.compare(0, 12, "HTTP/1.1 101") != 0) {
    closesocket(s);
    return false;
  }

  _socket = s;
  return true;
}

static void Close(void)
{
  std::lock_guard<std::mutex> lock(_mxSend);

  if (_socket != ~uintptr_t(0)) {
    closesocket((SOCKET)_socket);
    _socket = ~uintptr_t(0);
  }
  _bConnected = false;
}

static bool RecvAll(void *pBuffer, size_t size)
{
  char *p = (char *)pBuffer;

  while (size > 0) {
    const int n = recv((SOCKET)_socket, p, (int)size, 0);
    if (n <= 0) return false;
    p += n;
    size -= n;
  }
  return true;
}

static bool SendFrame(int iOpcode, const char *pData, size_t size)
{
  std::lock_guard<std::mutex> lock(_mxSend);
  if (_socket == ~uintptr_t(0)) return false;

  std::vector<char> frame;
  frame.reserve(size + 14);
  frame.push_back((char)(0x80 | iOpcode));

  if (size < 126) {
    frame.push_back((char)(0x80 | size));
  } else if (size < 65536) {
    frame.push_back((char)(0x80 | 126));
    frame.push_back((char)(size >> 8));
    frame.push_back((char)size);
  } else {
    frame.push_back((char)(0x80 | 127));
    for (int i = 7; i >= 0; --i) {
      frame.push_back((char)((uint64_t)size >> (8 * i)));
    }
  }

  _ulMask = _ulMask * 1664525u + 1013904223u;
  const uint32_t ulMask = _ulMask;
  const char *m = (const char *)&ulMask;
  frame.insert(frame.end(), m, m + 4);

  for (size_t i = 0; i < size; ++i) {
    frame.push_back(pData[i] ^ m[i & 3]);
  }

  const char *p = frame.data();
  size_t left = frame.size();

  while (left > 0) {
    const int n = ::send((SOCKET)_socket, p, (int)left, 0);
    if (n <= 0) return false;
    p += n;
    left -= n;
  }
  return true;
}

static void Run(void)
{
  std::string strPartial;

  while (!_bStop) {
    if (!_bConnected) {
      if (Open()) {
        _bConnected = true;
        ++_iGeneration;
      } else {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }
    }

    unsigned char head[2];
    if (!RecvAll(head, 2)) { Close(); continue; }

    const int iOpcode = head[0] & 0x0F;
    const bool bFin = (head[0] & 0x80) != 0;
    uint64_t size = head[1] & 0x7F;

    if (size == 126) {
      unsigned char ext[2];
      if (!RecvAll(ext, 2)) { Close(); continue; }
      size = (uint64_t(ext[0]) << 8) | ext[1];

    } else if (size == 127) {
      unsigned char ext[8];
      if (!RecvAll(ext, 8)) { Close(); continue; }
      size = 0;
      for (int i = 0; i < 8; ++i) size = (size << 8) | ext[i];
    }

    unsigned char mask[4] = {};
    const bool bMasked = (head[1] & 0x80) != 0;
    if (bMasked && !RecvAll(mask, 4)) { Close(); continue; }

    if (size > (64u << 20)) { Close(); continue; } // nothing legitimate is this big

    std::string strPayload((size_t)size, '\0');
    if (size > 0 && !RecvAll(&strPayload[0], strPayload.size())) { Close(); continue; }

    if (bMasked) {
      for (size_t i = 0; i < strPayload.size(); ++i) strPayload[i] ^= mask[i & 3];
    }

    switch (iOpcode) {
      case 0x0: // continuation
      case 0x1: // text
        strPartial += strPayload;
        if (bFin) {
          std::lock_guard<std::mutex> lock(_mxQueue);
          _queue.push_back(std::move(strPartial));
          strPartial.clear();
          if (_queue.size() > 4096) _queue.pop_front();
        }
        break;

      case 0x8: Close(); break; // close
      case 0x9: SendFrame(0xA, strPayload.data(), strPayload.size()); break; // ping
      default: break;
    }
  }
}

void Start(const char *strHost, int iPort)
{
  if (_bStarted) return;
  _bStarted = true;
  _strHost = strHost;
  _iPort = iPort;

  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  _thread = std::thread(Run);
}

void Stop(void)
{
  if (!_bStarted) return;
  _bStop = true;
  Close();
  if (_thread.joinable()) _thread.join();
  _bStarted = false;
}

bool Connected(void)
{
  return _bConnected;
}

int Generation(void)
{
  return _iGeneration;
}

bool Send(const std::string &strText)
{
  if (!_bConnected) return false;

  if (!SendFrame(0x1, strText.data(), strText.size())) {
    Close();
    return false;
  }
  return true;
}

bool Poll(std::string &strMessage)
{
  std::lock_guard<std::mutex> lock(_mxQueue);
  if (_queue.empty()) return false;

  strMessage = std::move(_queue.front());
  _queue.pop_front();
  return true;
}

}; // namespace

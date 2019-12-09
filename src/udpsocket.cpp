#include "udpsocket.hpp"
#include "engine.hpp"

namespace Sb {
      void UdpSocket::create(uint16_t const localPort, std::shared_ptr<UdpSocketIf> const& client) {
            std::shared_ptr<UdpSocket> ref = std::make_shared<UdpSocket>(client);
            ref->bindAndAdd(ref, localPort, client);
      }

      void UdpSocket::create(InetDest const& dest, std::shared_ptr<UdpSocketIf> const& client) {
            std::shared_ptr<UdpSocket> ref = std::make_shared<UdpSocket>(client);
            ref->connectAndAdd(ref, dest, client);
      }

      UdpSocket::UdpSocket(std::shared_ptr<UdpSocketIf> const& client) : Socket(UDP), client(client) {
            pErrorThrow(fd);
      }

      UdpSocket::~UdpSocket() {
      }

      void UdpSocket::connectAndAdd(std::shared_ptr<UdpSocket> const& me, InetDest const& dest, std::shared_ptr<UdpSocketIf> const& client) {
            client->udpSocket = me;
            makeTransparent();
            std::shared_ptr<Socket> sockRef = me;
            Engine::add(sockRef);
            destAddr = dest;
            connect(destAddr);
            client->connected(destAddr);
      }

      void UdpSocket::bindAndAdd(std::shared_ptr<UdpSocket> const& me, uint16_t const localPort, std::shared_ptr<UdpSocketIf> const& client) {
            client->udpSocket = me;
            makeTransparent();
            std::shared_ptr<Socket> sockRef = me;
            Engine::add(sockRef);
            bind(localPort);
      }

      void UdpSocket::handleRead() {
            std::lock_guard<std::mutex> sync(readLock);
            Bytes data(MAX_PACKET_SIZE);
            InetDest from = {{{}}, 0};
            const auto actuallyReceived = receiveDatagram(from, data);
            if (actuallyReceived < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                  logDebug("UdpClient::handleRead would block");
                  return;
            }
            client->received(from, data);
      }

      void UdpSocket::queueWrite(const InetDest& dest, const Bytes& data) {
            std::lock_guard<std::mutex> sync(writeLock);
            writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(dest), Bytes(data)));
            handleWrite();
      }

      void UdpSocket::queueWrite(const Bytes& data) {
            std::lock_guard<std::mutex> sync(writeLock);
            writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(destAddr), Bytes(data)));
            handleWrite();
      }


      void UdpSocket::disconnect() {
            logDebug("UdpSocket::disconnect() " + std::to_string(fd));
            Engine::remove(self);
      }

      void UdpSocket::doWrite(InetDest const& dest, Bytes const& data) {
            const auto actuallySent = sendDatagram(dest, data);
            pErrorLog(actuallySent, fd);
            if (actuallySent == -1) {
                  if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(dest), Bytes(data)));
                        return;
                  } else {
                        logDebug(std::string("UdpSocket::doWrite failed completely"));
                        client->notSent(dest, data);
                  }
            }
            decltype(actuallySent) dataLen = data.size();
            if (actuallySent == dataLen) {
            } else if (actuallySent > 0) {
                  logDebug("Partial write of " + std::to_string(actuallySent) + " out of " + std::to_string(dataLen));
                  writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(dest), Bytes(data.begin() + actuallySent, data.end())));
            }
      }

      void UdpSocket::handleWrite() {
            for (; ;) {
                  if (writeQueue.size() == 0) {
                        client->writeComplete();
                        return;
                  }
                  const auto data = writeQueue.front();
                  writeQueue.pop_front();
                  doWrite(data.first, data.second);
            }
      }

      bool UdpSocket::waitingOutEvent() {
            return false;
      }

      void UdpSocket::handleError() {
            logDebug("UdpSocket::handleError() is closed");
            client->disconnected();
      }
}

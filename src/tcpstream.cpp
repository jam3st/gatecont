#include "logger.hpp"
#include "tcpstream.hpp"

namespace Sb {
      void TcpStream::create(std::shared_ptr<TcpStreamIf> const& client, int const fd) {
            auto ref = std::make_shared<TcpStream>(client, fd);
            client->tcpStream = ref;
            ref->connected = true;
            ref->originalDestination();
            std::shared_ptr<Socket> sockRef = ref;
            Engine::add(sockRef);
      }

      void TcpStream::create(std::shared_ptr<TcpStreamIf> const& client, InetDest const& dest) {
            auto ref = std::make_shared<TcpStream>(client);
            client->tcpStream = ref;
            auto const err = ref->connect(dest);
            if(err >= 0) {
                  ref->connected = true;
            } else if(err == -1) {
                  ref->connected = false;
            } else {
                  pErrorLog(err, ref->fd);
                  return;
            }
            std::shared_ptr<Socket> sockRef = ref;
            Engine::add(sockRef);
      }

      TcpStream::TcpStream(std::shared_ptr<TcpStreamIf> const& client) : Socket(TCP), client(client) {
      }

      TcpStream::TcpStream(std::shared_ptr<TcpStreamIf> const& client, int const fd) : Socket(TCP, fd), client(client) {
            makeNonBlocking();
            keepAlive();
      }

      TcpStream::~TcpStream() {
            std::lock_guard<std::mutex> syncWrite(writeLock);
            std::lock_guard<std::mutex> syncRead(readLock);
            client->disconnected();
            client = nullptr;
      }

      void TcpStream::handleRead() {
            std::lock_guard<std::mutex> sync(readLock);
            for(; ;) {
                  Bytes data(MAX_PACKET_SIZE);
                  auto const actuallyRead = read(data);
                  if(actuallyRead > 0) {
                        if(client) {
                              client->received(data);
                        }
                   } else if(actuallyRead == 0) {
                        break;
                  } else if(actuallyRead == -1) {
                        break;
                  } else {
                        break;
                  }
            }
      }

      void TcpStream::handleWrite() {
            std::lock_guard<std::mutex> sync(writeLock);
            writeTriggered = false;
            blocked = false;
            if(connected) {
                  ssize_t totalWritten = 0;
                  bool wasEmpty = (writeQueue.size() == 0);
                  for(; ;) {
                        if(writeQueue.size() == 0) {
                              break;
                        } else {
                              auto const data = writeQueue.front();
                              writeQueue.pop_front();
                              auto const actuallySent = write(data);
                              if(actuallySent >= 0) {
                                    if((actuallySent - data.size()) != 0) {
                                          writeQueue.push_front(Bytes(data.begin() + actuallySent, data.end()));
                                    }
                                    totalWritten += actuallySent;
                              } else if(actuallySent == -1) {
                                    writeQueue.push_front(data);
                                    blocked = true;
                                    break;
                              } else {
                                    break;
                              }
                        }
                  }
                  bool isEmpty = (writeQueue.size() == 0);
                  if(!once || (!wasEmpty && isEmpty)) {
                        once = true;
                  }
            } else {
                  connected = true;
                  if(writeQueue.size() > 0) {
                        writeTriggered = true;
                        Engine::triggerWrites(this);
                  }
            }
      }

      bool TcpStream::waitingOutEvent() {
            std::lock_guard<std::mutex> sync(writeLock);
            return (blocked || !once || !connected) && !disconnecting;
      }


      void TcpStream::handleError() {
            getLastError();
            disconnect();
      }

      void TcpStream::disconnect() {
            std::lock_guard<std::mutex> sync(writeLock);
            if(!disconnecting) {
                  disconnecting = true;
                  Engine::remove(self);
            }
      }

      void TcpStream::queueWrite(Bytes const& data) {
            std::lock_guard<std::mutex> sync(writeLock);
            if(data.size() == 0) {
                  return;
            }
            writeQueue.push_back(data);
            if(connected && !blocked && !writeTriggered) {
                  writeTriggered = true;
                  Engine::triggerWrites(this);
            }
      }

      bool TcpStream::writeQueueEmpty() {
            std::lock_guard<std::mutex> sync(writeLock);
            return writeQueue.size() == 0;
      }

      bool TcpStream::didConnect() const {
            return connected;
      }

      InetDest TcpStream::endPoint() const {
            return originalDestination();
      }
}


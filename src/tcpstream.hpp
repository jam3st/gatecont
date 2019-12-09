#pragma once
#include <functional>
#include <deque>
#include "engine.hpp"
#include "socket.hpp"
#include "types.hpp"

namespace Sb {
      class TcpStream;

      class TcpStreamIf : public std::enable_shared_from_this<TcpStreamIf> {
      public:
            virtual ~TcpStreamIf() {}
            friend class TcpStream;
            virtual void received(Bytes const&) = 0;
            virtual void writeComplete() = 0;
            virtual void disconnected() = 0;
      protected:
            std::weak_ptr<TcpStream> tcpStream;
      };

      class TcpStream : public Socket {
      public:
            static void create(std::shared_ptr<TcpStreamIf> const& client, int const fd);
            static void create(std::shared_ptr<TcpStreamIf> const& client, InetDest const&dest);
            void queueWrite(const Bytes&data);
            void disconnect();
            TcpStream(std::shared_ptr<TcpStreamIf> const& client);
            TcpStream(std::shared_ptr<TcpStreamIf> const& client, int const fd);
            virtual ~TcpStream() override;
            bool didConnect() const;
            InetDest endPoint() const;
            bool writeQueueEmpty();
      protected:
            virtual void handleRead() override;
            virtual void handleWrite() override;
            virtual void handleError() override;
            virtual bool waitingOutEvent() override;

      private:
            std::shared_ptr<TcpStreamIf> client;
            std::mutex writeLock;
            std::mutex readLock;
            std::deque<Bytes> writeQueue;
            bool blocked = false;
            bool once = false;
            bool writeTriggered = false;
            bool connected = false;
            bool disconnecting = false;
      };
}


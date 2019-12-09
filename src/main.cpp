#include <iostream>
#include <functional>
#include <unistd.h>
#include "engine.hpp"
#include "tcplistener.hpp"
#include "udpsocket.hpp"
#include <random>
#include <list>
#include <chrono>

using namespace Sb;
using namespace std::chrono_literals;

void runUnit(const std::string id, std::function<void()> what) {
      try {
            Engine::init();
            what();
            Engine::start();
      } catch(const std::exception& e) {
            std::cerr << e.what() << "\n";
      } catch(...) {
            std::cerr << id << " " << "exception" << std::endl;
      }
}

class EchoUdp : public UdpSocketIf {
public:
    virtual ~EchoUdp() {
    }

    virtual void connected(const InetDest&) override {
    }

    virtual void disconnected() override {
    }

    virtual void received(InetDest const& addr, Bytes const& datagram) override {
        updateUser(addr);
        if(Stop == datagram) {
            broadcast(Stop);
        } else if(Open == datagram) {
            broadcast(Open);
        } else if(Upda == datagram) {
            broadcast(Upda);
        } else if(Ping == datagram) {
            sendReply(addr, Pong);
        } else if(Boot == datagram) {
            broadcast(Boot);
        } else if(Opea == datagram) {
            broadcast(Opea);
        } else if(Clea == datagram) {
            broadcast(Clea);
        } else if(Opwe == datagram) {
            broadcast(Opwe);
        } else if(Clwe == datagram) {
            broadcast(Clwe);
        } else if(Rand == datagram) {
            broadcast(Rand);
        } else if(Clse == datagram) {
            broadcast(Clse);
        } else if(Util == datagram) {
            broadcast(Util);
        } else if(Bing == datagram) {
            auto ignore = system("/jffs/asterisk/bin/bing.sh");
        } else if(Ring == datagram) {
            sendReply(addr, Sing);
            auto ignore = system("/jffs/asterisk/bin/ring.sh");
        } else {
            logDebug(addr.toString() + " " + std::string( reinterpret_cast<char const*>(datagram.data()), datagram.size() ));
        }
    }

    virtual void notSent(InetDest const& addr, const Bytes&) override {
    }

    virtual void writeComplete() override {
    }

    virtual void disconnect() override {
    }
private:
    void broadcast(Bytes const& message) {
        auto ref = udpSocket.lock();
        if(ref) {
            std::lock_guard<std::mutex> sync(lock);
            for(auto it = users.begin(); it != users.end(); ++it) {
                ref->queueWrite(it->addr, message);
            }
        }
    }

    void sendReply(InetDest const& addr, Bytes const& message) {
        auto ref = udpSocket.lock();
        if(ref) {
            ref->queueWrite(addr, message);
        }
    }


    void updateUser(InetDest const& addr) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> sync(lock);
        bool found = false;
        auto it = users.begin();
        while(it != users.end()) {
            if(addr == it->addr) {
                it->ts = now;
                found = true;
                ++it;
            } else if(now - it->ts > ClientDisconnectTimeout) {
                logDebug("Lost connection " + addr.toString());
                it = users.erase(it);
            } else {
                ++it;
            }
        }
        if(!found) {
            logDebug("New connection " + addr.toString());
            users.push_back({addr, now});
        }
    }
private:
    struct Users {
        InetDest addr;
        std::chrono::steady_clock::time_point ts;
    };
    const Bytes Ping{ 'p', 'i', 'n', 'g'};
    const Bytes Pong{ 'p', 'o', 'n', 'g'};

    const Bytes Stop{ 's', 't', 'o', 'p'};
    const Bytes Open{ 'o', 'p', 'e', 'n'};
    const Bytes Boot{ 'b', 'o', 'o', 't'};
    const Bytes Bing{ 'b', 'i', 'n', 'g'};

    const Bytes Ring{ 'r', 'i', 'n', 'g'};
    const Bytes Sing{ 's', 'i', 'n', 'g'};

    const Bytes Opea{ 'o', 'p', 'e', 'a'};
    const Bytes Clea{ 'c', 'l', 'e', 'a'};
    const Bytes Opwe{ 'o', 'p', 'w', 'e'};
    const Bytes Clwe{ 'c', 'l', 'w', 'e'};
    const Bytes Rand{ 'r', 'a', 'n', 'd'};
    const Bytes Util{ 'u', 't', 'i', 'l'};
    const Bytes Clse{ 'c', 'l', 's', 'e'};

    const Bytes Upda{ 'u', 'p', 'd', 'a'};

    const decltype (5min) ClientDisconnectTimeout = 1min;
    std::list<Users> users;
    std::mutex lock;
};

class UdpCommand : public UdpSocketIf {
public:
    UdpCommand(char const* command, char const* response) {
        for(auto c = command; *c != '\0'; ++c) {
            this->command.push_back(*c);
        }
        for(auto r = response; *r != '\0'; ++r) {
            this->response.push_back(*r);
        }
    }
      
    virtual ~UdpCommand() {
    }

    virtual void connected(const InetDest& addr) override {
       auto ref = udpSocket.lock();
        if(ref) {
            ref->queueWrite(command);
        }
    }

    virtual void disconnected() override {
    }

    virtual void notSent(InetDest const& addr, const Bytes&) override {
    }

    virtual void writeComplete() override {
    }

    virtual void disconnect() override {
    }
    virtual void received(InetDest const& addr, Bytes const& datagram) override {
            logDebug("received from connection " + addr.toString() + " " + std::string( reinterpret_cast<char const*>(datagram.data()), datagram.size() ));
            if(datagram == response) {
                exit(0);
            }
            exit(1);
    }
private:
    Bytes command;
    Bytes response;
};

int main(const int argc, const char* const argv[]) {
    ::close(0);

    if(argc == 1) { 
              runUnit("echoudp", []() {
                    std::shared_ptr<UdpSocketIf> ref = std::make_shared<EchoUdp>();
                    UdpSocket::create(1024, ref);
              });
    } else if(argc == 3) {
              runUnit("sendudp", [&argv]() {
                    std::shared_ptr<UdpSocketIf> ref = std::make_shared<UdpCommand>(argv[1], argv[2]);
                    UdpSocket::create(Socket::destFromString("127.0.0.1", 1024), ref);
              });
    } else {
        std::cerr << "Use the source Luke\n";
    }
    return 0;
}

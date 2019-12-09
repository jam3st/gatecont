#include <sys/epoll.h>
#include <csignal>
#include <unistd.h>
#include <pthread.h>
#include <algorithm>
#include <chrono>
#include <sys/timerfd.h>
#include "engine.hpp"

namespace Sb {
      Engine* Engine::theEngine = nullptr;

      Engine::Worker::~Worker() {
            thread.detach();
      }

      Engine::Engine() : stopping(false), activeCount(0), epollTid(std::this_thread::get_id()), epollThreadHandle(::pthread_self()),
                         epollFd(::epoll_create1(EPOLL_CLOEXEC)) {
            Logger::start();
            Logger::setMask(Logger::LogType::EVERYTHING);
            assert(epollFd >= 0, "Failed to create epollFd");
      }

      Engine::~Engine() {
            ::signal(SIGUSR1, SIG_DFL);
            ::close(epollFd);
            logDebug("Engine::~Engine()");
            Logger::stop();
      }

      void Engine::start(int minWorkersPerCpu) {
            std::unique_ptr<Engine> tmp(Engine::theEngine);
            theEngine->doInit(minWorkersPerCpu);
            Engine::theEngine = nullptr;
      }

      void Engine::stop() {
            if(Engine::theEngine != nullptr) {
                  Engine::theEngine->doStop();
            }
      }

      void signalHandler(int) {
            Engine::theEngine->doSignalHandler();
      }

      void Engine::doSignalHandler() {
            if(!stopping && epollTid == std::this_thread::get_id()) {
                  std::lock_guard<std::mutex> sync(evHashLock);
                  stopping = true;
            }
      }

      void Engine::startWorkers(int minWorkersPerCpu) {
            std::lock_guard<std::mutex> sync(evHashLock);
            int const initialNumThreadsToSpawn = std::thread::hardware_concurrency() * minWorkersPerCpu + 1;
            for(int i = 0; i < initialNumThreadsToSpawn; ++i) {
                  slaves.push_back(new Worker(Engine::doWork));
            }
      }

      void Engine::stopWorkers() {
            bool waiting = true;
            for(int i = 1024 * std::thread::hardware_concurrency(); i > 0 && waiting; i--) {
                  waiting = false;
                  for(auto& slave : slaves) {
                        if(!slave->exited) {
                              sem.signal();
                              waiting = true;
                        }
                  }
            }
            assert(!waiting, "Engine::stopWorkers failed to stop");
            for(auto& slave : slaves) {
                  delete slave;
            }
      }

      void Engine::doInit(int const minWorkersPerCpu) {
            assert(eventHash.size() > NUM_ENGINE_EVENTS || timerEvent, "Engine::doInit Need to Add() something before Go().");
            std::signal(SIGPIPE, signalHandler);
            for(int i = SIGHUP; i < _NSIG; ++i) {
                  std::signal(i, SIG_IGN);
            }
            std::signal(SIGQUIT, signalHandler);
            std::signal(SIGINT, signalHandler);
            std::signal(SIGTERM, signalHandler);
            std::signal(SIGUSR1, signalHandler);
            startWorkers(minWorkersPerCpu);
            doEpoll();
            stopWorkers();
            for(int i = SIGHUP; i < _NSIG; ++i) {
                  std::signal(i, SIG_DFL);
            }
            timerEvent.reset();
            eventHash.clear();
            eventQueue.clear();
      }

      void Engine::triggerWrites(Socket* const what) {
            if(Engine::theEngine == nullptr) {
                  throw std::runtime_error("Engine::triggerWrites Please call Engine::Init() first");
            }
            Engine::theEngine->doTriggerWrites(what);
      }

      void Engine::newEvent(uint64_t const evId, uint32_t const events) {
            {
                  std::lock_guard<std::mutex> sync(evHashLock);
                  auto it = eventHash.find(evId);
                  if(it == eventHash.end()) {
                        return;
                  }
                  {
                        std::lock_guard<std::mutex> sync(eqLock);
                        eventQueue.push_back(Event(it->second, std::bind(&Engine::run, this, it->second.get(), events)));
                  }
            }
            sem.signal();
      }

      void Engine::doTriggerWrites(Socket* const what) {
            newEvent(what->evId, EPOLLOUT);
      }

      void Engine::runAsync(Event const& event) {
            if (Engine::theEngine == nullptr) {
                  throw std::runtime_error("Engine::runAsync Please call Engine::Init() first");
            }
            Engine::theEngine->doRunAsync(event);
      }

      void Engine::doRunAsync(Event const& event) {
            {
                  std::lock_guard<std::mutex> sync(eqLock);
                  eventQueue.push_back(event);
            }
            sem.signal();
      }


      void Engine::doAdd(std::shared_ptr<Socket> const& what) {
            if(!stopping) {
                  auto const epollOut = what->waitingOutEvent();
                  std::lock_guard<std::mutex> sync(evHashLock);
                  what->self = what;
                  what->evId = ++evCounter;
                  assert(eventHash.find(what->evId) == eventHash.end(), "Already cound id");
                  bool const added = eventHash.emplace(what->evId, what).second;
                  assert(added, "Already exists in hash");
                  epoll_event event = {(epollOut ? EPOLLOUT : 0) | EPOLLONESHOT | EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLET, {.u64 = what->evId}};
                  pErrorThrow(::epoll_ctl(epollFd, EPOLL_CTL_ADD, what->fd, &event), epollFd);
            }
      }

      void Engine::add(std::shared_ptr<Socket> const& what) {
            if(Engine::theEngine == nullptr) {
                  throw std::runtime_error("Engine::add Please call Engine::Init() first");
            }
            theEngine->doAdd(what);
      }

      void Engine::doRemove(std::weak_ptr<Socket> const& what) {
            if(!stopping) {
                  auto const ref = what.lock();
                  if(ref) {
                          std::lock_guard<std::mutex> sync(evHashLock);
                          auto it = eventHash.find(ref->evId);
                          assert(it != eventHash.end(), "Not found for removal " + std::to_string(ref->evId));
                          eventHash.erase(it);
                  }
            }
      }

      void Engine::remove(std::weak_ptr<Socket> const& what) {
            if(Engine::theEngine == nullptr) {
                  throw std::runtime_error("Engine::remove Please call Engine::Init() first");
            }
            theEngine->doRemove(what);
      }

      void Engine::init() {
            if(Engine::theEngine == nullptr) {
                  Engine::theEngine = new Engine();
            }
      }


      void Engine::doStop() {
            if(!stopping && epollTid != std::this_thread::get_id()) {
                  ::pthread_kill(epollThreadHandle, SIGUSR1);
            }
      }

      void Engine::run(Socket* const sock, const uint32_t events) {
            {
                  std::lock_guard<std::mutex> sync(evHashLock);
                  auto const it = eventHash.find(sock->evId);
                  if(it == eventHash.end()) {
                        return;
                  }
            }
            if((events & EPOLLOUT) != 0) {
                  sock->handleWrite();
            }
            if((events & (EPOLLIN)) != 0) {
                  sock->handleRead();
            }
            if((events & EPOLLRDHUP) != 0 || (events & EPOLLERR) != 0) {
                  sock->handleError();
                  pErrorLog(sock->getLastError(), sock->fd);
            } else {
                  bool const needOut = sock->waitingOutEvent();
                  std::lock_guard<std::mutex> sync(evHashLock);
                  auto const it = eventHash.find(sock->evId);
                  if(it != eventHash.end()) {
                        epoll_event event = {(needOut ? EPOLLOUT : 0) | EPOLLONESHOT | EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLET, {.u64 = sock->evId}};
                        pErrorThrow(::epoll_ctl(epollFd, EPOLL_CTL_MOD, sock->fd, &event), epollFd);
                  }
            }
      }

      void Engine::worker(Worker& me) {
            try {
                  while(!stopping) {
                        sem.wait();
                        Event event({},
                        []() {
                        });
                        {
                              std::lock_guard<std::mutex> sync(eqLock);
                              if(eventQueue.size() != 0) {
                                    event = eventQueue.front();
                                    eventQueue.pop_front();
                              }
                        }
                        activeCount++;
                        event();
                        activeCount--;
                        if(eventHash.size() == NUM_ENGINE_EVENTS && !timerEvent && activeCount == 0) {
                              doStop();
                              break;
                        }
                  }
            } catch(std::exception& e) {
                  logError(std::string("Engine::worker threw a ") + e.what());
                  doStop();
            } catch(...) {
                  logError("Unknown exception in Engine::worker");
                  doStop();
            }
            me.exited = true;
      }

      void Engine::doWork(Worker* me) noexcept {
            theEngine->worker(*me);
      }

      void Engine::doEpoll() {
            try {
                  while(!stopping) {
                        epoll_event epEvents[EPOLL_EVENTS_PER_RUN];
                        int num = epoll_wait(epollFd, epEvents, EPOLL_EVENTS_PER_RUN, -1);
                        if(stopping) {
                              break;
                        }
                        if(num >= 0) {
                              for(int i = 0; i < num; ++i) {
                                    if(epEvents[i].data.u64 == timerEvId) {
                                    } else {
                                          newEvent(epEvents[i].data.u64, epEvents[i].events);
                                    }
                              }
                        } else if(num == -1 && (errno == EINTR || errno == EAGAIN)) {
                              continue;
                        } else {
                              pErrorThrow(num, epollFd);
                        }
                  }
            } catch(std::exception& e) {
                  logError(std::string("Engine::epollThread threw a ") + e.what());
                  stop();
            } catch(...) {
                  logError("Unknown exception in Engine::epollThread");
                  stop();
            }
      }
}

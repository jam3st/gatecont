﻿#pragma once
#include <deque>
#include <memory>
#include <thread>
#include <map>
#include "semaphore.hpp"
#include "event.hpp"
#include "socket.hpp"

namespace Sb {
      class Engine final {
      public:
            static void start(int minWorkersPerCpu = 4);
            static void stop();
            static void init();
            static void add(std::shared_ptr<Socket> const& what);
            static void remove(std::weak_ptr<Socket> const& what);
            static void triggerWrites(Socket* const what);
            static void runAsync(Event const& event);
            ~Engine();
            void startWorkers(int const minWorkersPerCpu);
            void stopWorkers();
      private:
            class Worker;

            Engine();
            friend void signalHandler(int);
            void doEpoll(Worker* me) noexcept;
            static void doWork(Worker* me) noexcept;
            void doStop();
            void doSignalHandler();
            void doInit(int const minWorkersPerCpu);
            void newEvent(uint64_t const evId, uint32_t const events);
            void run(Socket* const sock, const uint32_t events);
            void doEpoll();
            void worker(Worker&me);
            void doAdd(std::shared_ptr<Socket> const& what);
            void doRemove(std::weak_ptr<Socket> const& what);
            void doTriggerWrites(Socket* const what);
            void doRunAsync(Event const& event);

      private:
            static Engine* theEngine;
            std::mutex timerLock;
            Semaphore sem;
            std::mutex eqLock;
            std::deque<Event> eventQueue;
            std::unique_ptr<Event> timerEvent;
            bool stopping;
            std::atomic_int activeCount;
            std::thread::id epollTid;
            std::thread::native_handle_type epollThreadHandle;
            int epollFd = -1;
            std::vector<Worker*> slaves;
            std::mutex evHashLock;
            std::map<uint64_t, std::shared_ptr<Socket>> eventHash;
      private:
            uint64_t const timerEvId = 0;
            uint64_t evCounter = timerEvId;
            std::size_t const NUM_ENGINE_EVENTS = 0;
            std::size_t const EPOLL_EVENTS_PER_RUN = 128;
      };

      class Engine::Worker {
      public:
            Worker() = delete;

            Worker(void (func(Worker*) noexcept)) : thread(func, this) {
            }

            ~Worker();
            std::thread thread;
            bool exited;
      };
}

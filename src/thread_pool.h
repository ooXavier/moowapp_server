/*!
 * \file thread_pool.h
 * \brief Thread Pool management class
 * \author Xavier ETCHEBER
 */

#ifndef MOOWAPP_STATS_THREAD_POOL_H_
#define MOOWAPP_STATS_THREAD_POOL_H_

#include <thread>
#include <mutex>
#include <condition_variable>

// Boost
#include <boost/thread/thread.hpp> // Thread system
#include <boost/asio.hpp> // Service system

 
class ThreadPool;
 
/*!
 * \class Worker
 * \brief Worker thread objects for a task to complete.
 *
 */
class Worker {
public:
   Worker(ThreadPool &s) : pool(s) { }
   void operator()();
private:
   ThreadPool &pool;
};
 
/*!
 * \class ThreadPool
 * \brief Pool of workers with task to accomplish.
 *
 */
class ThreadPool {
public:
   ThreadPool(size_t);
   template<class F>
   void enqueue(F f);
   ~ThreadPool();
private:
   friend class Worker;
 
   // need to keep track of threads so we can join them
   std::vector< std::thread > workers;
 
   // the task queue
   std::deque< std::function<void()> > tasks;
 
   // synchronization
   std::mutex queue_mutex;
   std::condition_variable condition;
   bool stop;
};

#endif // MOOWAPP_STATS_THREAD_POOL_H_
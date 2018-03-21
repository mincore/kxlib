/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: kx_task.h
 *     Created: 2014-02-04 14:48
 * Description:
 * ===================================================
 */
#ifndef _KX_TASK_H
#define _KX_TASK_H

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <map>
#include <atomic>
#include <limits>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>

typedef std::function<void(void)> Task;

namespace kx {

template<typename T>
class Queue {
public:
    void stop() {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_stop = true;
        m_cond.notify_all();
    }

    bool push(const T &t, size_t *size_before_push = NULL) {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (m_stop)
            return false;
        if (size_before_push)
            *size_before_push = m_Queue.size();
        m_Queue.emplace_back(t);
        m_cond.notify_one();
        return true;
    }

    bool pop(T &t, size_t *size_after_pop = NULL) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cond.wait(lk, [this]{ return m_stop || !m_Queue.empty(); });
        if (m_stop)
            return false;
        t = m_Queue.front();
        m_Queue.pop_front();
        if (size_after_pop)
            *size_after_pop = m_Queue.size();
        return true;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::deque<T> m_Queue;
    bool m_stop = false;
};

template<class T>
class Ring {
public:
    Ring (size_t cap): m_data(cap) {}

    bool empty() { return m_count == 0; }
    bool full() { return m_count == m_data.size(); }

    bool push(const T &t) {
        if (full()) return false;
        m_data[m_next] = t;
        m_next = (m_next + 1) % m_data.size();
        m_count++;
        return true;
    }

    bool pop(T &t) {
        if (empty()) return false;
        size_t index = (m_next - m_count + m_data.size()) % m_data.size();
        m_count--;
        t = m_data[index];
        return true;
    }

private:
    std::vector<T> m_data;
    size_t m_count = 0;
    size_t m_next = 0;
};

template<typename T>
class Chan {
public:
    Chan(size_t size = 1): m_ring(size) {}

    void push(const T &t) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_wcond.wait(lk, [this]{ return !m_ring.full(); });
        m_ring.push(t);
        m_rcond.notify_one();
    }

    T pop() {
        T t;
        std::unique_lock<std::mutex> lk(m_mutex);
        m_rcond.wait(lk, [this]{ return !m_ring.empty(); });
        m_ring.pop(t);
        m_wcond.notify_one();
        return t;
    }

private:
    Chan(const Chan &Chan) = delete;
    const Chan& operator=(const Chan&) = delete;

private:
    std::mutex m_mutex;
    std::condition_variable m_rcond;
    std::condition_variable m_wcond;
    Ring<T> m_ring;
};

static inline long long now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
            ).count();
}

class SpinLock
{
public:
    void lock() { while(m_flag.test_and_set(std::memory_order_acquire)){} }
    void unlock() { m_flag.clear(std::memory_order_release); }
private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

class TaskPool {
public:
    typedef Queue<Task> TaskQ;

    bool push(const Task &task, const char *name) {
        if (name == NULL || *name == 0) {
            return m_taskq.push(task);
        }

        TaskQ *q = get_taskq(name);
        size_t size_before_push;
        if (!q->push(task, &size_before_push)) {
            return false;
        }

        if (size_before_push == 0) {
            return m_taskq.push(std::bind(RunTaskQ, &m_taskq, q));
        }

        return true;
    }

    bool pop(Task &task) {
        return m_taskq.pop(task);
    }

    void stop() {
        m_taskq.stop();
    }

private:
    static void RunTaskQ(TaskQ* holder, TaskQ *q) {
        Task task;
        size_t size_after_pop;
        if (!q->pop(task, &size_after_pop))
            return;

        task();
        if (size_after_pop > 0) {
            holder->push(std::bind(RunTaskQ, holder, q));
        }
    }

    TaskQ *get_taskq(const std::string &name) {
        std::lock_guard<SpinLock> lk(m_spinlock);
        return &m_map[name];
    }

private:
    SpinLock m_spinlock;
    std::unordered_map<std::string, TaskQ> m_map;
    TaskQ m_taskq;
};

class TaskTimer {
public:
    bool push(const Task &task, long long delay) {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (m_stop)
            return false;
        m_time_map.emplace(now() + delay, task);
        if (now() >= m_time_map.begin()->first)
            m_cond.notify_one();
        return true;
    }

    bool pop(Task &task) {
        while (1) {
            std::unique_lock<std::mutex> lk(m_mutex);
            if (m_stop) {
                return false;
            }

            auto delay = m_time_map.empty() ? 1000 : m_time_map.begin()->first - now();
            if (delay > 0) {
                m_cond.wait_for(lk, std::chrono::milliseconds(delay), [this]{
                        return m_stop || (!m_time_map.empty() && m_time_map.begin()->first >= now());
                });
                continue;
            }
            task = m_time_map.begin()->second;
            m_time_map.erase(m_time_map.begin());
            return true;
        }
    }

    void stop() {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_stop = true;
        m_cond.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::multimap<long long, Task> m_time_map;
    bool m_stop = false;
};

template<typename T>
class ThreadPool {
public:
    ThreadPool(int nthread) {
        for (int i=0; i<nthread; i++) {
            m_threads.push_back(std::thread([this]{
                Task task;
                while(m_t.pop(task)) {
                    task();
                }
            }));
        }
    }

    ~ThreadPool() {
        m_t.stop();
        for (auto &thread : m_threads) {
            thread.join();
        }
    }

    template<typename Arg>
    bool push(const Task &task, Arg arg)  {
        return m_t.push(task, arg);
    }

private:
    std::vector<std::thread> m_threads;
    T m_t;
};

void RunTask(const Task &task, const char *queue_name = NULL)
{
    static ThreadPool<TaskPool> threadpool(std::thread::hardware_concurrency());
    threadpool.push(task, queue_name);
}

void RunTask(const Task &task, long long delay)
{
    static ThreadPool<TaskTimer> threadpool(1);
    threadpool.push(task, delay);
}

}
#endif

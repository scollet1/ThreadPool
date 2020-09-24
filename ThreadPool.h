#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#pragma once

#include <mutex>
#include <queue>
#include <atomic>
#include <future>
#include <thread>
#include <vector>
#include <typeinfo>
#include <functional>
#include <type_traits>
#include <condition_variable>

/* ThreadPool class */
class ThreadPool {

public:
	static ThreadPool* get_instance() {	
		if (!instance)
			instance = new ThreadPool;
		return instance;
	}

	//add any arg # function to queue
	template <typename Func, typename... Args>
	auto submit(Func&& f, Args&&... args) {
		//get return type of the function
		using RetType = std::invoke_result_t<Func, Args...>;

		auto task = std::make_shared<std::packaged_task<RetType()>>([&f, &args...]() {
			return f(std::forward<Args>(args)...);
		}); {
			// lock jobQueue mutex, add job to the job queue 
			std::scoped_lock<std::mutex> lock(m_jobMutex);

			//place the job into the queue
			m_jobQueue.emplace([task]() {
				(*task)();
			});
		}
		m_notifier.notify_one();

		return task->get_future();
	}
	
	/* utility functions */
	std::size_t getThreadCount() const {
		return m_threads.size();
	}

private:
	static ThreadPool* instance;

	ThreadPool() {
		m_shutdown.store(false, std::memory_order_relaxed);
		//m_shutdown = false;
		createThreads(1);
	}

	ThreadPool(std::size_t numThreads) {
		m_shutdown.store(false, std::memory_order_relaxed);
		//m_shutdown = false;
		createThreads(numThreads);
	}

	~ThreadPool() {
		m_shutdown.store(true, std::memory_order_relaxed);
		//m_shutdown = true;
		m_notifier.notify_all();

		for (std::thread& th : m_threads) {
			th.join();
		}
	}

	void createThreads(std::size_t numThreads) {
		m_threads.reserve(numThreads);
		for (int i = 0; i != numThreads; ++i) {
			m_threads.emplace_back(std::thread([this]() {
				while (true) {
					Job job;

					{
						std::unique_lock<std::mutex> lock(m_jobMutex);
						m_notifier.wait(lock, [this] {
							return (
								!m_jobQueue.empty() || m_shutdown.load(std::memory_order_relaxed)
							);
						});

						if (m_shutdown.load(std::memory_order_relaxed)) break;

						job = std::move(m_jobQueue.front());

						m_jobQueue.pop();
					}
					job();
				}
			}));
		}
	}

	using Job = std::function<void()>;
	std::vector<std::thread> m_threads;
	std::queue<Job> m_jobQueue;
	std::condition_variable m_notifier;
	std::mutex m_jobMutex;
	std::atomic<bool> m_shutdown;

}; /* end ThreadPool Class */

ThreadPool* ThreadPool::instance = 0;
ThreadPool* ThreadPool = ThreadPool->get_instance();

#endif
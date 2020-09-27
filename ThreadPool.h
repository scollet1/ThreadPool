#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#pragma once

#include <type_traits>
#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <future>
#include <vector>
#include <typeinfo>
#include <functional>

/* ThreadPool class */
class ThreadPool {

public:
	//add any arg # function to queue
	template <typename Func, typename... Args>
	static auto submit(Func&& f, Args&&... args) {
		//get return type of the function
		using RetType = std::invoke_result_t<Func, Args...>;

		auto task = std::make_shared<std::packaged_task<RetType()>>([&f, &args...]() {
			return f(std::forward<Args>(args)...);
		});

		{
			// lock jobQueue mutex, add job to the job queue
			std::scoped_lock<std::mutex> lock(m_job_mutex);

			//place the job into the queue
			m_job_queue.emplace([task]() {
				(*task)();
			});
		}

		m_notifier.notify_one();

		return task->get_future();
	}

	/* utility functions */
	static std::size_t get_thread_count() const {
		return m_threads.size();
	}

private:
	static ThreadPool* instance;

	ThreadPool() {
		m_shutdown.store(false, std::memory_order_relaxed);
		//m_shutdown = false;
		create_threads(1);
	}

	ThreadPool(std::size_t num_threads) {
		m_shutdown.store(false, std::memory_order_relaxed);
		//m_shutdown = false;
		create_threads(num_threads);
	}

	~ThreadPool() {
		m_shutdown.store(true, std::memory_order_relaxed);
		//m_shutdown = true;
		m_notifier.notify_all();

		for (std::thread& th : m_threads) {
			th.join();
		}
	}

	static void create_threads(std::size_t num_threads) {
		m_threads.reserve(num_threads);
		for (int i = 0; i != num_threads; i++) {
			m_threads.emplace_back(std::thread([this]() {
				while (true) {
					Job job;

					{
						std::unique_lock<std::mutex> lock(m_job_mutex);
						m_notifier.wait(lock, [this] {
							return (
								!m_jobQueue.empty() || m_shutdown.load(std::memory_order_relaxed)
							);
						});

						if (m_shutdown.load(std::memory_order_relaxed)) break;

						job = std::move(m_job_queue.front());

						m_job_queue.pop();
					}

					job();
				}
			}));
		}
	}

	using Job = std::function<void()>;
	std::vector<std::thread> m_threads;
	std::queue<Job> m_job_queue;
	std::condition_variable m_notifier;
	std::mutex m_job_mutex;
	std::atomic<bool> m_shutdown;

}; /* end ThreadPool Class */

#endif
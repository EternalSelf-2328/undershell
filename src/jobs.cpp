// SPDX-License-Identifier: GPL-3.0-or-later
#include "jobs.hpp"

#include "common.hpp"

#include <cstdint>
#include <sys/eventfd.h>
#include <unistd.h>

namespace undershell {

Jobs::Jobs(int threads) {
  m_eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  for (int i = 0; i < threads; ++i) m_threads.emplace_back([this] { loop(); });
}

Jobs::~Jobs() {
  {
    std::lock_guard lock(m_mutex);
    m_stop = true;
  }
  m_cv.notify_all();
  for (auto& t : m_threads) t.join();
  if (m_eventFd >= 0) close(m_eventFd);
}

void Jobs::run(Work work) {
  {
    std::lock_guard lock(m_mutex);
    m_queue.push_back(std::move(work));
  }
  m_cv.notify_one();
}

void Jobs::loop() {
  for (;;) {
    Work work;
    {
      std::unique_lock lock(m_mutex);
      m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
      if (m_stop) return;
      work = std::move(m_queue.front());
      m_queue.pop_front();
    }
    Done done;
    try {
      done = work();
    } catch (const std::exception& e) {
      US_WARN("background job failed: {}", e.what());
    }
    if (!done) continue;
    {
      std::lock_guard lock(m_mutex);
      m_done.push_back(std::move(done));
    }
    const std::uint64_t one = 1;
    (void)!write(m_eventFd, &one, sizeof(one));
  }
}

void Jobs::dispatch() {
  std::uint64_t n = 0;
  (void)!read(m_eventFd, &n, sizeof(n));
  std::deque<Done> done;
  {
    std::lock_guard lock(m_mutex);
    done.swap(m_done);
  }
  for (auto& d : done) d();
}

}  // namespace undershell

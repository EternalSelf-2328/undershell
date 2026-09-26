// SPDX-License-Identifier: GPL-3.0-or-later
// Background work that must never stall a frame (downloads, image decoding,
// lyrics lookups). A job runs on a worker thread and returns a completion
// closure that the main loop runs on its own thread (where GL lives).
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace undershell {

class Jobs {
public:
  using Done = std::function<void()>;
  using Work = std::function<Done()>;

  explicit Jobs(int threads = 2);
  ~Jobs();
  Jobs(const Jobs&) = delete;
  Jobs& operator=(const Jobs&) = delete;

  void run(Work work);
  [[nodiscard]] int fd() const { return m_eventFd; }  // readable when completions are pending
  void dispatch();                                    // runs completions (main thread)

private:
  void loop();
  std::vector<std::thread> m_threads;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<Work> m_queue;
  std::deque<Done> m_done;
  bool m_stop = false;
  int m_eventFd = -1;
};

}  // namespace undershell

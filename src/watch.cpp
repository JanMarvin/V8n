#include "v8n.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>

/* While JavaScript runs, R cannot react to Ctrl+C or Esc. A watcher thread
 * wakes up every 100ms during a run. It asks V8 to interrupt the script on
 * the main thread, where R is asked about a pending user interrupt, and it
 * stops the script once a timeout has passed. V8 may be stopped from another
 * thread with TerminateExecution(), and RequestInterrupt() runs its callback
 * on the main thread. */

enum stop_reason_type : int {
  STOP_NONE = 0,
  STOP_INTERRUPT = 1,
  STOP_TIMEOUT = 2,
  STOP_MEMORY = 3
};

/* Never destroyed: at exit, destroying a condition variable that the watcher
 * thread still waits on would block (glibc waits for all waiters). */
static std::mutex& watch_mutex = *new std::mutex();
static std::condition_variable& watch_cv = *new std::condition_variable();
static bool watch_active = false;
static bool watch_has_deadline = false;
static std::chrono::steady_clock::time_point watch_deadline;

/* Only used on the main thread */
static bool watch_thread_started = false;
static int watch_depth = 0;
static double watch_timeout = 0;

static std::atomic<int> stop_reason(STOP_NONE);

static void set_stop_reason(stop_reason_type reason){
  int none = STOP_NONE;
  stop_reason.compare_exchange_strong(none, reason);
}

static void r_check_interrupt(void*){
  R_CheckUserInterrupt();
}

/* Runs on the main thread, inside JavaScript. R_ToplevelExec() keeps the
 * interrupt of R from jumping over the JavaScript frames. */
static void check_interrupt(v8::Isolate* iso, void*){
  if(watch_depth == 0)
    return;
  if(!R_ToplevelExec(r_check_interrupt, nullptr)){
    set_stop_reason(STOP_INTERRUPT);
    iso->TerminateExecution();
  }
}

static void watch_loop(){
  std::unique_lock<std::mutex> lock(watch_mutex);
  for(;;){
    watch_cv.wait(lock, []{ return watch_active; });
    while(watch_active){
      watch_cv.wait_for(lock, std::chrono::milliseconds(100));
      if(!watch_active)
        break;
      if(watch_has_deadline && std::chrono::steady_clock::now() >= watch_deadline){
        watch_has_deadline = false;
        set_stop_reason(STOP_TIMEOUT);
        isolate->TerminateExecution();
      } else {
        isolate->RequestInterrupt(check_interrupt, nullptr);
      }
    }
  }
}

/* V8 calls this before its heap is full, which would otherwise abort the
 * process. The script is stopped, and the limit is raised so that V8 can
 * finish; it returns to the original limit after the next collections. The
 * increase is at most 256MB, to stay within the 4GB of pointer compression. */
static size_t near_heap_limit(void*, size_t current_limit, size_t){
  set_stop_reason(STOP_MEMORY);
  isolate->TerminateExecution();
  const size_t max_increase = static_cast<size_t>(256) * 1024 * 1024;
  return current_limit + std::min(current_limit / 2, max_increase);
}

void watch_install(v8::Isolate* iso){
  iso->AddNearHeapLimitCallback(near_heap_limit, nullptr);
  iso->AutomaticallyRestoreInitialHeapLimit(0.5);
}

watch_scope::watch_scope(double timeout) : outermost(watch_depth == 0) {
  watch_depth++;
  if(!outermost)
    return;
  stop_reason = STOP_NONE;
  watch_timeout = timeout;
  if(!watch_thread_started){
    std::thread(watch_loop).detach();
    watch_thread_started = true;
  }
  {
    std::lock_guard<std::mutex> lock(watch_mutex);
    watch_active = true;
    watch_has_deadline = std::isfinite(timeout) && timeout > 0;
    if(watch_has_deadline){
      std::chrono::duration<double> seconds(timeout);
      watch_deadline = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(seconds);
    }
  }
  watch_cv.notify_one();
}

watch_scope::~watch_scope(){
  watch_depth--;
  if(!outermost)
    return;
  {
    std::lock_guard<std::mutex> lock(watch_mutex);
    watch_active = false;
  }
  watch_cv.notify_one();
  if(stop_reason != STOP_NONE)
    isolate->CancelTerminateExecution();
}

bool watch_scope::is_outermost() const {
  return outermost;
}

bool watch_stopped(){
  return stop_reason != STOP_NONE;
}

void watch_scope::check() const {
  switch(stop_reason.load()){
  case STOP_INTERRUPT:
    throw Rcpp::internal::InterruptedException();
  case STOP_TIMEOUT: {
    std::ostringstream msg;
    msg << "JavaScript did not finish within the timeout of " << watch_timeout << " seconds.";
    throw std::runtime_error(msg.str());
  }
  case STOP_MEMORY:
    throw std::runtime_error("JavaScript ran out of memory.");
  default:
    return;
  }
}

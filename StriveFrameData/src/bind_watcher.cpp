#include "bind_watcher.h"

#include <bitset>
#include <condition_variable>
#include <mutex>

#include <Windows.h>

LRESULT CALLBACK KeyboadCallback(int nCode, WPARAM wParam, LPARAM lParam);

class BindWorker {
  std::atomic<bool> isRunning;
  HHOOK kbh = nullptr;
  std::thread worker_thread;

  void run() {
    MSG msg;
    if (!kbh) kbh = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboadCallback, GetModuleHandle(NULL), 0);

    while (GetMessage(&msg, nullptr, 0, 0) > 0 && isRunning.load()) {
    }

    UnhookWindowsHookEx(kbh);
  }

public:
  BindWorker() {
    isRunning     = true;
    worker_thread = std::thread(&BindWorker::run, this);
  }
  ~BindWorker() {
    isRunning   = false;
    auto handle = GetThreadId(worker_thread.native_handle());
    PostThreadMessage(handle, WM_USER, 0, 0);
    worker_thread.join();
  }
  LRESULT callNext(int nCode, WPARAM wParam, LPARAM lParam) { return CallNextHookEx(kbh, nCode, wParam, lParam); }
};

class BindWatcher {
  BindWorker worker;
  std::mutex state_mutex;
  std::condition_variable await_con;
  std::bitset<0xff> filter;
  std::vector<int> inputs;

  bool markKey(DWORD code) {
    std::lock_guard state_lock(state_mutex);
    if (!filter.test(code)) return false;
    if (inputs.size() > 100) inputs.erase(inputs.begin()); // only store latest 100 inputs
    inputs.push_back(code);
    return true;
  }

  BindWatcher() = default;

public:
  ~BindWatcher() = default;

  void addToFilter(unsigned long code) { filter.set(code, true); }

  std::vector<int> getInputs(bool await) {
    std::unique_lock state_lock(state_mutex);
    if (await) {
      while (inputs.empty())
        await_con.wait(state_lock);
    }
    std::vector<int> result;
    result.swap(inputs);
    return result;
  }

  LRESULT callback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
      KBDLLHOOKSTRUCT &k = *(KBDLLHOOKSTRUCT *)lParam;

      if (markKey(k.vkCode)) await_con.notify_all();
    }
    return worker.callNext(nCode, wParam, lParam);
  }

  static BindWatcher &instance() {
    static BindWatcher singleton;
    return singleton;
  }
};

LRESULT CALLBACK KeyboadCallback(int nCode, WPARAM wParam, LPARAM lParam) {
  return BindWatcher::instance().callback(nCode, wParam, lParam);
}

void BindWatcherI::addToFilter(unsigned long code) {
  BindWatcher::instance().addToFilter(code);
}

std::vector<int> BindWatcherI::getInputs(bool await) {
  return BindWatcher::instance().getInputs(await);
}
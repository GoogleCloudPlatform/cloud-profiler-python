// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "profiler.h"

#include <errno.h>
#include <pythread.h>
#include <sys/time.h>
#include <sys/ucontext.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "clock.h"
#include "log.h"

AsyncSafeTraceMultiset *Profiler::fixed_traces_ = nullptr;
std::atomic<int> Profiler::unknown_stack_count_;
GetThreadStateFunc get_thread_state_func = PyGILState_GetThisThreadState;
bool CPUProfiler::fork_handlers_registered_;

namespace {

struct PyObjectDecReffer {
  void operator()(PyObject *py_object) const {
    // Ensures the current thread is ready to call the Python C API.
    PyGILState_STATE gil_state = PyGILState_Ensure();
    Py_XDECREF(py_object);
    PyGILState_Release(gil_state);
  }
};

typedef std::unique_ptr<PyObject, PyObjectDecReffer> PyObjectRef;

// Helper class to store and reset errno when in a signal handler.
class ErrnoRaii {
 public:
  ErrnoRaii() { stored_errno_ = errno; }
  // Not copyable or assignable.
  ErrnoRaii(const ErrnoRaii &) = delete;
  ErrnoRaii &operator=(const ErrnoRaii &) = delete;

  ~ErrnoRaii() { errno = stored_errno_; }

 private:
  int stored_errno_;
};

}  // namespace

destructor CodeDeallocHook::old_code_dealloc_ = nullptr;
std::unordered_map<PyCodeObject *, FuncLoc>
    *CodeDeallocHook::deallocated_code_ = nullptr;

void CodeDeallocHook::CodeDealloc(PyObject *py_object) {
  FuncLoc func_loc;
  PyCodeObject *code_object = reinterpret_cast<PyCodeObject *>(py_object);
  GetFuncLoc(code_object, &func_loc);
  deallocated_code_->insert(std::make_pair(code_object, func_loc));

  old_code_dealloc_(py_object);
}

void CodeDeallocHook::Reset() {
  if (deallocated_code_ == nullptr) {
    deallocated_code_ = new std::unordered_map<PyCodeObject *, FuncLoc>;
  } else {
    deallocated_code_->clear();
  }
}

bool CodeDeallocHook::Find(PyCodeObject *pointer, FuncLoc *func_loc) {
  auto recorded_code = deallocated_code_->find(pointer);
  if (recorded_code == deallocated_code_->end()) {
    return false;
  }
  *func_loc = recorded_code->second;
  return true;
}

// This method schedules the SIGPROF timer to go off every specified interval.
bool SignalHandler::SetSigprofInterval(int64_t period_usec) {
  static struct itimerval timer;
  timer.it_interval.tv_sec = period_usec / kMicrosPerSecond;
  timer.it_interval.tv_usec = period_usec % kMicrosPerSecond;
  timer.it_value = timer.it_interval;
  if (setitimer(ITIMER_PROF, &timer, NULL) == -1) {
    LogError("Failed to set ITIMER_PROF: %s", strerror(errno));
    return false;
  }
  return true;
}

struct sigaction SignalHandler::SetAction(void (*action)(int, siginfo_t *,
                                                         void *)) {
  struct sigaction sa;
  sa.sa_handler = nullptr;
  sa.sa_sigaction = action;
  sa.sa_flags = SA_RESTART | SA_SIGINFO;

  sigemptyset(&sa.sa_mask);

  struct sigaction old_handler;
  if (sigaction(SIGPROF, &sa, &old_handler) != 0) {
    LogError("Failed to set SIGPROF handler: %s", strerror(errno));
    return old_handler;
  }

  return old_handler;
}

const char *CallTraceErrorToName(CallTraceErrors err) {
  switch (err) {
    case kNoPyState:
      return "[Unknown - No Python thread state]";
    default:
      return "[Unknown]";
  }
}

void Profiler::Handle(int signum, siginfo_t *info, void *context) {
  // Gets around -Wunused-parameter.
  (void)signum;
  (void)info;
  (void)context;

  ErrnoRaii err_storage;  // stores and resets errno

  CallTrace trace;
  CallFrame frames[kMaxFramesToCapture];
  trace.frames = frames;
  trace.num_frames = 0;

  // PyGILState_GetThisThreadState uses pthread_getspecific which is not
  // guaranteed to be async-signal-safe per POSIX. Some issues can be
  // found at https://sourceware.org/glibc/wiki/TLSandSignals.
  // TODO: check if the limitations are practical here and if
  // there are ways to avoid the problems.
  PyThreadState *ts = get_thread_state_func();

  if (ts == nullptr) {
    frames[0].lineno = kNoPyState;
    frames[0].py_code = nullptr;
    trace.num_frames = 1;
  } else {
    // We are running in the context of the thread interrupted by the signal
    // so the frame object for the current thread is stable.
    PyFrameObject *frame = ts->frame;
    int num_frames = 0;
    while (frame != nullptr && num_frames < kMaxFramesToCapture) {
      frames[num_frames].lineno = frame->f_lineno;
      frames[num_frames].py_code = frame->f_code;
      num_frames++;
      frame = frame->f_back;
    }
    trace.num_frames = num_frames;
  }

  if (!fixed_traces_->Add(&trace)) {
    unknown_stack_count_++;
    return;
  }
}

void GetFuncLoc(PyCodeObject *code_object, FuncLoc *func_loc) {
#if PY_MAJOR_VERSION < 3
  const char *name = PyString_AS_STRING(code_object->co_name);
  const char *filename = PyString_AS_STRING(code_object->co_filename);
#else
  // Note that PyUnicode_AsUTF8 caches the char array in the unicodeobject
  // and the memory is released when the unicodeobject is deallocated.
  const char *name = PyUnicode_AsUTF8(code_object->co_name);
  const char *filename = PyUnicode_AsUTF8(code_object->co_filename);
#endif
  func_loc->name = name != nullptr ? name : "unknown";
  func_loc->filename = filename != nullptr ? filename : "unknown";
}

// Should be called when GIL is held if PyCode_Type.tp_dealloc is modified,
// otherwise PyCode_Type.tp_dealloc may be updating
// CodeDeallocHook.deallocated_code_ in another thread.
void Profiler::Reset() {
  if (fixed_traces_ == nullptr) {
    fixed_traces_ = new AsyncSafeTraceMultiset();
  } else {
    fixed_traces_->Reset();
  }
  CodeDeallocHook::Reset();
  unknown_stack_count_ = 0;
  handler_.SetAction(&Profiler::Handle);
}

// Must be called when GIL is held.
PyObject *Profiler::PythonTraces() {
#if PY_MAJOR_VERSION >= 3
  // Asserts that GIL is held in debug mode.
  assert(PyGILState_Check());
#endif
  if (unknown_stack_count_ > 0) {
    CallFrame fakeFrame = {kUnknown, nullptr};
    aggregated_traces_.Add(1, &fakeFrame, unknown_stack_count_);
  }

  PyObjectRef py_traces(PyDict_New());
  if (py_traces == nullptr) {
    return nullptr;
  }
  for (const auto &trace : aggregated_traces_) {
    PyObjectRef py_frames(PyTuple_New(trace.first.size()));
    if (py_frames == nullptr) {
      return nullptr;
    }

    for (size_t i = 0; i < trace.first.size(); i++) {
      const auto &frame = trace.first[i];
      FuncLoc func_loc;
      PyCodeObject *pointer = frame.py_code;
      if (pointer == nullptr) {
        func_loc = {
            CallTraceErrorToName(static_cast<CallTraceErrors>(frame.lineno)),
            ""};
      } else {
        // All PyCodeObjects deallocated during profiling should be recorded
        // by CodeDeallocHook. As we are holding GIL, no deallocation can happen
        // elsewhere now. It's safe to assume that a PyCodeObject pointer not
        // recorded by CodeDeallocHook points to a live object.
        // TODO: If multiple code objects are allocated at the same
        // address, the func_loc stored by CodeDeallocHook may not belong to the
        // sampled frame. At least we should mark the func_loc as invalid if we
        // see an address is reused, probably by hooking PyCode_Type.tp_alloc.
        if (!CodeDeallocHook::Find(pointer, &func_loc)) {
          GetFuncLoc(pointer, &func_loc);
        }
      }
      PyObject *py_frame =
          Py_BuildValue("(ssi)", func_loc.name.c_str(),
                        func_loc.filename.c_str(), frame.lineno);
      if (py_frame == nullptr) {
        return nullptr;
      }
      // PyTuple_SET_ITEM is like PyTuple_SetItem(), but does no error checking.
      // Error checking is unnecessary here as we are filling precreated brand
      // new tuple. Note that PyTuple_SET_ITEM does NOT increase the reference
      // count for the inserted item. We are no longer responsible for
      // decreasing the reference count of py_frame. It'll be decreased when
      // py_frames is deallocated.
      PyTuple_SET_ITEM(py_frames.get(), i, py_frame);
    }
    uint64_t count = trace.second;
    PyObject *py_count = PyDict_GetItem(py_traces.get(), py_frames.get());
    if (py_count != nullptr) {
      uint64_t previous_count = PyLong_AsUnsignedLong(py_count);
      if (PyErr_Occurred()) {
        return nullptr;
      }
      count += previous_count;
    }
    PyObjectRef trace_count(PyLong_FromUnsignedLongLong(count));
    // PyDict_SetItem increases the reference count for both key and item. We
    // are responsible for decreasing the reference count for py_frames and
    // trace_count.
    if (PyDict_SetItem(py_traces.get(), py_frames.get(), trace_count.get()) <
        0) {
      return nullptr;
    }
  }

  return py_traces.release();
}

bool AlmostThere(const struct timespec &finish, const struct timespec &lap) {
  // Determine if there is time for another lap before reaching the
  // finish line. Have a margin of multiple laps to ensure we do not
  // overrun the finish line.
  int64_t margin_laps = 2;

  struct timespec now = DefaultClock()->Now();
  struct timespec laps = {lap.tv_sec * margin_laps, lap.tv_nsec * margin_laps};

  return TimeLessThan(finish, TimeAdd(now, laps));
}

PyObject *CPUProfiler::Collect() {
  Reset();
  // Hooks to PyCode_Type.tp_dealloc so that a PyCodeObject is recorded before
  // being deallocated. The hook is cancelled when dealloc_hook goes out of
  // scope.
  CodeDeallocHook dealloc_hook;

  if (!Start()) {
    return nullptr;
  }
  // Releases GIL so that the user threads can execute.
  Py_BEGIN_ALLOW_THREADS;

  Clock *clock = DefaultClock();
  // Flush the async table every 100 ms
  struct timespec flush_interval = {0, 100 * 1000 * 1000};  // 100 millisec
  struct timespec finish_line =
      TimeAdd(clock->Now(), NanosToTimeSpec(duration_nanos_));

  // Sleep until finish_line, but wakeup periodically to flush the
  // internal tables.
  while (!AlmostThere(finish_line, flush_interval)) {
    clock->SleepFor(flush_interval);
    Flush();
  }
  clock->SleepUntil(finish_line);
  Stop();
  // Delay to allow last signals to be processed.
  clock->SleepUntil(TimeAdd(finish_line, flush_interval));
  Flush();
  // Reacquire the GIL.
  Py_END_ALLOW_THREADS;

  PyObject *traces = PythonTraces();
  return traces;
}

bool CPUProfiler::Start() {
  int period_usec = period_nanos_ / 1000;
  return handler_.SetSigprofInterval(period_usec);
}

void CPUProfiler::Stop() {
  handler_.SetSigprofInterval(0);
  // Breaks encapsulation, but whatever.
  signal(SIGPROF, SIG_IGN);
}

// Blocks the SIGPROF signal for the calling thread.
void BlockSigprof() {
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGPROF);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);
}

// Unblocks the SIGPROF signal for the calling thread.
void UnblockSigprof() {
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGPROF);
  pthread_sigmask(SIG_UNBLOCK, &signals, nullptr);
}

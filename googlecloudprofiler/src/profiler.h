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

#ifndef GOOGLECLOUDPROFILER_SRC_PROFILER_H_
#define GOOGLECLOUDPROFILER_SRC_PROFILER_H_

#include <Python.h>
#include <signal.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "stacktraces.h"

struct FuncLoc {
  std::string name;
  std::string filename;
};

void GetFuncLoc(PyCodeObject *code_object, FuncLoc *func_loc);

// Blocks the SIGPROF signal for the calling thread.
void BlockSigprof();

// Unblocks the SIGPROF signal for the calling thread.
void UnblockSigprof();

class SignalHandler {
 public:
  SignalHandler() {}

  // Not copyable or assignable.
  SignalHandler(const SignalHandler &) = delete;
  SignalHandler &operator=(const SignalHandler &) = delete;

  struct sigaction SetAction(void (*sigaction)(int, siginfo_t *, void *));

  bool SetSigprofInterval(int64_t period_usec);
};

class CodeDeallocHook {
 public:
  // The constructor must be called when GIL is held.
  CodeDeallocHook() {
    Reset();
    old_code_dealloc_ = PyCode_Type.tp_dealloc;
    PyCode_Type.tp_dealloc = &CodeDealloc;
  }
  // Not copyable or assignable.
  CodeDeallocHook(const CodeDeallocHook &) = delete;
  CodeDeallocHook &operator=(const CodeDeallocHook &) = delete;

  // The destructor must be called when GIL is held.
  ~CodeDeallocHook() { PyCode_Type.tp_dealloc = old_code_dealloc_; }

  // A wrapper function on PyCode_Type.tp_dealloc that records the code object
  // to deallocated_code_ before the actual deallocation.
  static void CodeDealloc(PyObject *py_object);

  // The first call to Reset() allocates deallocated_code_. Subsequent calls
  // clear deallocated_code_. When PyCode_Type.tp_dealloc points to CodeDealloc,
  // this function must be called when GIL is held, otherwise another thread
  // may be updating allocated_code_ during PyCodeObject deallocation.
  static void Reset();

  // If the given pointer exists in deallocated_code_ as key, assign the value
  // to func_loc and return true, otherwise return false. When
  // PyCode_Type.tp_dealloc points to CodeDealloc, this function must be called
  // when GIL is held, otherwise another thread may be updating
  // allocated_code_ during PyCodeObject deallocation.
  static bool Find(PyCodeObject *pointer, FuncLoc *func_loc);

 private:
  // When PyCode_Type.tp_dealloc points to CodeDealloc, a code object is
  // recorded in this map before being deallocated. The map maps a code object
  // pointer to function information of interest.
  static std::unordered_map<PyCodeObject *, FuncLoc> *deallocated_code_;

  static destructor old_code_dealloc_;
};

typedef PyThreadState *(*GetThreadStateFunc)();

// get_thread_state_func defaults to PyGILState_GetThisThreadState. It's
// declared here so that PyGILState_GetThisThreadState can be stubbed in tests.
extern GetThreadStateFunc get_thread_state_func;

class Profiler {
 public:
  Profiler(int64_t duration_nanos, int64_t period_nanos)
      : duration_nanos_(duration_nanos), period_nanos_(period_nanos) {
    Reset();
  }
  // Not copyable or assignable.
  Profiler(const Profiler &) = delete;
  Profiler &operator=(const Profiler &) = delete;

  virtual ~Profiler() {}

  // Collects performance data.
  // Implicitly does a Reset() before starting collection.
  virtual PyObject *Collect() = 0;

  // Returns the traces as a Python dictionary object, which maps a trace to its
  // count.
  PyObject *PythonTraces();

  // Signal handler, which records the current stack trace.
  static void Handle(int signum, siginfo_t *info, void *context);

  // Resets internal state to support data collection.
  void Reset();

  // Migrates data from fixed internal table into growable data structure.
  // Returns number of entries extracted.
  int Flush() { return HarvestSamples(fixed_traces_, &aggregated_traces_); }

 protected:
  SignalHandler handler_;
  int64_t duration_nanos_;
  int64_t period_nanos_;

 private:
  // Points to a fixed multiset of traces used during collection. This
  // is allocated on the first call to Reset(). Will be reused by
  // subsequent allocations. Cannot be deallocated as it could be in
  // use by other threads, triggered from a signal handler.
  static AsyncSafeTraceMultiset *fixed_traces_;

  // Aggregated profile data, populated using data extracted from
  // fixed_traces.
  TraceMultiset aggregated_traces_;

  static std::atomic<int> unknown_stack_count_;
};

// CPUProfiler collects cpu profiles by setting up a CPU timer and
// collecting a sample each time it is triggered (via SIGPROF).
class CPUProfiler : public Profiler {
 public:
  CPUProfiler(int64_t duration_nanos, int64_t period_nanos)
      : Profiler(duration_nanos, period_nanos) {
    // When a fork runs longer than the signal interval, it gets interrupted by
    // the signal and then retry. This will never end until the profiler
    // thread stops sending the signal. In unlucky cases, the profiler
    // thread gets blocked on acquiring the memory lock, which is held by
    // fork. The process may thus hang unpredictably long.
    // The fix is to block the signal for the calling thread before fork and
    // reenable it after fork. The caveat is that forks will not be sampled.
    if (!fork_handlers_registered_) {
      pthread_atfork(&BlockSigprof, &UnblockSigprof, &UnblockSigprof);
      // Updating fork_handlers_registered_ here is not thread safe. It's
      // fine because the profiler is only allowed to start once, which means
      // that CPUProfiler is only created by a single thread.
      fork_handlers_registered_ = true;
    }
  }
  // Not copyable or assignable.
  CPUProfiler(const CPUProfiler &) = delete;
  CPUProfiler &operator=(const CPUProfiler &) = delete;

  // Collects profiling data.
  PyObject *Collect() override;

 private:
  // Initiates data collection at a fixed interval.
  bool Start();

  // Stops data collection.
  void Stop();

  static bool fork_handlers_registered_;
};

#endif  // GOOGLECLOUDPROFILER_SRC_PROFILER_H_

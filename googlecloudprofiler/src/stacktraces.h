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

#ifndef GOOGLECLOUDPROFILER_SRC_STACKTRACES_H_
#define GOOGLECLOUDPROFILER_SRC_STACKTRACES_H_

#include <Python.h>
#include <frameobject.h>

#include <atomic>
#include <mutex>  // NOLINT
#include <unordered_map>
#include <vector>

typedef struct {
  int lineno;
  PyCodeObject *py_code;
} CallFrame;

typedef struct {
  int num_frames;
  CallFrame *frames;
} CallTrace;

enum CallTraceErrors {
  kUnknown = 0,
  kNoPyState = -1,
};

// Maximum number of frames to store from the stack traces sampled.
const int kMaxFramesToCapture = 128;

uint64_t CalculateHash(int num_frames, const CallFrame *frame);
bool Equal(int num_frames, const CallFrame *f1, const CallFrame *f2);

// Multiset of stack traces. There is a maximum number of distinct
// traces that can be held, return by MaxEntries();
//
// The Add() operation is async-safe, but will fail and return false
// if there is no room to store the trace.
//
// The Extract() operation will remove a specific entry, and it can
// run concurrently with multiple Add() operations. Multiple
// invocations of Extract() cannot be executed concurrently.
//
// The synchronization is implemented by using a sentinel count value
// to reserve entries. Add() will reserve the first available entry,
// save the stack frame, and then release the entry for other calls to
// Add() or Extract(). Extract() will reserve the entry, wait until no
// additions are in progress, and then release the entry to be reused
// by a subsequent call to Add(). It is important for Extract() to
// wait until no additions are in progress to avoid releasing the
// entry while another thread is inspecting it.
class AsyncSafeTraceMultiset {
 public:
  AsyncSafeTraceMultiset() { Reset(); }
  // Not copyable or assignable.
  AsyncSafeTraceMultiset(const AsyncSafeTraceMultiset &) = delete;
  AsyncSafeTraceMultiset &operator=(const AsyncSafeTraceMultiset &) = delete;

  void Reset() { memset(traces_, 0, sizeof(traces_)); }

  // Adds a trace to the set. If it is already present, increments its
  // count. This operation is thread safe and async safe.
  bool Add(const CallTrace *trace);

  // Extracts a trace from the array. frames must point to at least
  // max_frames contiguous frames. It will return the number of frames
  // written starting at frames[0], up to max_frames. Returns 0 if
  // there is no valid trace at this location.  This operation is
  // thread safe with respect to Add() but only a single call to
  // Extract can be done at a time.
  int Extract(int location, int max_frames, CallFrame *frames, int64_t *count);

  int64_t MaxEntries() const { return kMaxStackTraces; }

 private:
  struct TraceData {
    // trace contains the frame count and a pointer to the frames. The
    // frames are stored in frame_buffer.
    CallTrace trace;
    // frame_buffer is the storage for stack frames.
    CallFrame frame_buffer[kMaxFramesToCapture];
    // Number of times a trace has been encountered.
    // 0 indicates that the trace is unused,
    // <0 values are reserved, used for concurrency control.
    std::atomic<int64_t> count;
    // Number of active attempts to increase the counter on the trace.
    std::atomic<int> active_updates;
  };

  // TODO: Re-evaluate MaxStackTraces, to minimize storage
  // consumption while maintaining good performance and avoiding
  // overflow.
  static const int kMaxStackTraces = 2048;

  // Sentinel to use as trace count while the frames are being updated.
  static const int64_t kTraceCountLocked = -1;

  TraceData traces_[kMaxStackTraces];
};

// TraceMultiset implements a growable multi-set of traces. It is not
// thread or async safe. Is it intended to be used to aggregate traces
// collected atomically from AsyncSafeTraceMultiset, which implements
// async and thread safe add/extract methods, but has fixed maximum
// size.
class TraceMultiset {
 private:
  struct TraceHash {
    std::size_t operator()(const std::vector<CallFrame> &t) const {
      return CalculateHash(t.size(), t.data());
    }
  };

  struct TraceEqual {
    bool operator()(const std::vector<CallFrame> &t1,
                    const std::vector<CallFrame> &t2) const {
      if (t1.size() != t2.size()) {
        return false;
      }
      return Equal(t1.size(), t1.data(), t2.data());
    }
  };

  typedef std::unordered_map<std::vector<CallFrame>, uint64_t, TraceHash,
                             TraceEqual>
      CountMap;

 public:
  TraceMultiset() {}
  // Not copyable or assignable.
  TraceMultiset(const TraceMultiset &) = delete;
  TraceMultiset &operator=(const TraceMultiset &) = delete;

  // Add a trace to the array. If it is already in the array,
  // increment its count.
  void Add(int num_frames, CallFrame *frames, int64_t count);

  typedef CountMap::iterator iterator;
  typedef CountMap::const_iterator const_iterator;

  iterator begin() { return traces_.begin(); }
  iterator end() { return traces_.end(); }

  const_iterator begin() const { return const_iterator(traces_.begin()); }
  const_iterator end() const { return const_iterator(traces_.end()); }

  iterator erase(iterator it) { return traces_.erase(it); }

  void Clear() { traces_.clear(); }

 private:
  CountMap traces_;
};

// HarvestSamples extracts traces from an asyncsafe trace multiset
// and copies them into a trace multiset. It returns the number of samples
// that were copied. This is thread-safe with respect to other threads adding
// samples into the asyncsafe set.
int HarvestSamples(AsyncSafeTraceMultiset *from, TraceMultiset *to);

#endif  // GOOGLECLOUDPROFILER_SRC_STACKTRACES_H_

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

#include "stacktraces.h"

bool AsyncSafeTraceMultiset::Add(const CallTrace *trace) {
  uint64_t hash_val = CalculateHash(trace->num_frames, trace->frames);
  for (int64_t i = 0; i < MaxEntries(); i++) {
    int64_t idx = (i + hash_val) % MaxEntries();
    auto &entry = traces_[idx];
    int64_t count_zero = 0;
    entry.active_updates.fetch_add(1, std::memory_order_acquire);
    int64_t count = entry.count.load(std::memory_order_acquire);
    switch (count) {
      case 0:
        if (entry.count.compare_exchange_weak(count_zero, kTraceCountLocked,
                                              std::memory_order_relaxed)) {
          // This entry is reserved, there is no danger of interacting
          // with Extract, so decrement active_updates early.
          entry.active_updates.fetch_sub(1, std::memory_order_release);

          // memcpy is not async safe
          CallFrame *fb = entry.frame_buffer;
          for (int frame_num = 0; frame_num < trace->num_frames; ++frame_num) {
            fb[frame_num].lineno = trace->frames[frame_num].lineno;
            fb[frame_num].py_code = trace->frames[frame_num].py_code;
          }
          entry.trace.frames = fb;
          entry.trace.num_frames = trace->num_frames;
          entry.count.store(int64_t(1), std::memory_order_release);
          return true;
        }
        break;
      case kTraceCountLocked:
        // This entry is being updated by another thread. Move on.
        // Worst case we may end with multiple entries with the same trace.
        break;
      default:
        if (trace->num_frames == entry.trace.num_frames &&
            Equal(trace->num_frames, trace->frames, entry.trace.frames)) {
          // Bump using a compare-swap instead of fetch_add to ensure
          // it hasn't been locked by a thread doing Extract().
          // Reload count in case it was updated while we were
          // examining the trace.
          count = entry.count.load(std::memory_order_relaxed);
          if (count != kTraceCountLocked &&
              entry.count.compare_exchange_weak(count, count + 1,
                                                std::memory_order_relaxed)) {
            entry.active_updates.fetch_sub(1, std::memory_order_release);
            return true;
          }
        }
    }
    // Did nothing, but we still need storage ordering between this
    // store and preceding loads.
    entry.active_updates.fetch_sub(1, std::memory_order_release);
  }
  return false;
}

int AsyncSafeTraceMultiset::Extract(int location, int max_frames,
                                    CallFrame *frames, int64_t *count) {
  if (location < 0 || location >= MaxEntries()) {
    return 0;
  }
  auto &entry = traces_[location];
  int64_t c = entry.count.load(std::memory_order_acquire);
  if (c <= 0) {
    // Unused or in process of being updated, skip for now.
    return 0;
  }
  int num_frames = entry.trace.num_frames;
  if (num_frames > max_frames) {
    num_frames = max_frames;
  }

  c = entry.count.exchange(kTraceCountLocked, std::memory_order_acquire);

  for (int i = 0; i < num_frames; ++i) {
    frames[i].lineno = entry.trace.frames[i].lineno;
    frames[i].py_code = entry.trace.frames[i].py_code;
  }

  while (entry.active_updates.load(std::memory_order_acquire) != 0) {
    // spin
    // TODO: Introduce a limit to detect and break
    // deadlock
  }

  entry.count.store(0, std::memory_order_release);
  *count = c;
  return num_frames;
}

void TraceMultiset::Add(int num_frames, CallFrame *frames, int64_t count) {
  std::vector<CallFrame> trace(frames, frames + num_frames);

  auto entry = traces_.find(trace);
  if (entry != traces_.end()) {
    entry->second += count;
    return;
  }
  traces_.emplace(std::move(trace), count);
}

int HarvestSamples(AsyncSafeTraceMultiset *from, TraceMultiset *to) {
  int trace_count = 0;
  int64_t num_traces = from->MaxEntries();
  for (int64_t i = 0; i < num_traces; i++) {
    CallFrame frame[kMaxFramesToCapture];
    int64_t count;

    int num_frames = from->Extract(i, kMaxFramesToCapture, &frame[0], &count);
    if (num_frames > 0 && count > 0) {
      ++trace_count;
      to->Add(num_frames, &frame[0], count);
    }
  }
  return trace_count;
}

uint64_t CalculateHash(int num_frames, const CallFrame *frame) {
  uint64_t h = 0;
  for (int i = 0; i < num_frames; i++) {
    h += static_cast<uintptr_t>(frame[i].lineno);
    h += h << 10;
    h ^= h >> 6;
    h += reinterpret_cast<uintptr_t>(frame[i].py_code);
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  return h;
}

bool Equal(int num_frames, const CallFrame *f1, const CallFrame *f2) {
  for (int i = 0; i < num_frames; i++) {
    if (f1[i].lineno != f2[i].lineno || f1[i].py_code != f2[i].py_code) {
      return false;
    }
  }
  return true;
}

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

#ifndef GOOGLECLOUDPROFILER_SRC_CLOCK_H_
#define GOOGLECLOUDPROFILER_SRC_CLOCK_H_

#include <stdint.h>
#include <time.h>

static const int64_t kNanosPerSecond = 1000 * 1000 * 1000;
static const int64_t kMicrosPerSecond = 1000 * 1000;
static const int64_t kNanosPerMilli = 1000 * 1000;

// Clock interface that can be mocked for tests. The default implementation
// delegates to the system and so is thread-safe.
class Clock {
 public:
  virtual ~Clock() {}

  // Returns the current time.
  virtual struct timespec Now() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
  }

  // Blocks the current thread until the specified point in time.
  virtual void SleepUntil(struct timespec ts) {
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) > 0) {
    }
  }

  // Blocks the current thread for the specified duration.
  virtual void SleepFor(struct timespec ts) {
    while (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) > 0) {
    }
  }
};

struct timespec TimeAdd(const struct timespec t1, const struct timespec t2);
struct timespec NanosToTimeSpec(int64_t nanos);
bool TimeLessThan(const struct timespec &t1, const struct timespec &t2);

// Returns a singleton Clock instance which uses the system implementation.
Clock *DefaultClock();

#endif  // GOOGLECLOUDPROFILER_SRC_CLOCK_H_

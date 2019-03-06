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

#include "clock.h"

namespace {
Clock DefaultClockInstance;
}  // namespace

Clock *DefaultClock() { return &DefaultClockInstance; }

struct timespec TimeAdd(const struct timespec t1, const struct timespec t2) {
  struct timespec t = {t1.tv_sec + t2.tv_sec, t1.tv_nsec + t2.tv_nsec};
  if (t.tv_nsec > kNanosPerSecond) {
    t.tv_sec += t.tv_nsec / kNanosPerSecond;
    t.tv_nsec = t.tv_nsec % kNanosPerSecond;
  }
  return t;
}

bool TimeLessThan(const struct timespec &t1, const struct timespec &t2) {
  return (t1.tv_sec < t2.tv_sec) ||
         (t1.tv_sec == t2.tv_sec && t1.tv_nsec < t2.tv_nsec);
}

struct timespec NanosToTimeSpec(int64_t nanos) {
  time_t seconds = nanos / kNanosPerSecond;
  int32_t nano_seconds = nanos % kNanosPerSecond;
  return timespec{seconds, nano_seconds};
}

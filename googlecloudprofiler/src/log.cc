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

#include "log.h"

#include <Python.h>

void Log(const char *level, const char *fmt, ...) {
  // Ensures the current thread is ready to call the Python C API. GIL is
  // garanteed to be held.
  PyGILState_STATE gil_state = PyGILState_Ensure();
  static PyObject *logging = nullptr;
  if (logging == nullptr) {
    // GIL is held as PyGILState_Ensure is called above, no need to worry about
    // thread safety.
    logging = PyImport_ImportModuleNoBlock("logging");
  }
  if (logging == nullptr) {
    fputs(
        "googlecloudprofiler: failed to import logging module, logging "
        "is not enabled.\n",
        stderr);
    PyGILState_Release(gil_state);
    return;
  }
  char msg[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  PyObject_CallMethod(logging, const_cast<char *>(level),
                      const_cast<char *>("s"), msg);
  // Resets the Python state to be the same as it was prior to the
  // corresponding PyGILState_Ensure() call.
  PyGILState_Release(gil_state);
}

void LogError(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Log("error", fmt, ap);
  va_end(ap);
}

void LogWarning(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Log("warning", fmt, ap);
  va_end(ap);
}

void LogInfo(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Log("info", fmt, ap);
  va_end(ap);
}

void LogDebug(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Log("debug", fmt, ap);
  va_end(ap);
}

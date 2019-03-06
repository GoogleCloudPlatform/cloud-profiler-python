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

#include <Python.h>

#include "clock.h"
#include "profiler.h"

namespace {
PyObject* ProfileCPU(PyObject* self, PyObject* args) {
  uint64_t duration_nanos = 0;
  uint64_t period_msec = 0;
  if (!PyArg_ParseTuple(args, "LL", &duration_nanos, &period_msec)) {
    return nullptr;
  }

  CPUProfiler p(duration_nanos, period_msec * kNanosPerMilli);
  return p.Collect();
}

PyMethodDef ProfilerMethods[] = {
    {"profile_cpu", ProfileCPU, METH_VARARGS, "A function for CPU profiling."},
    {nullptr, nullptr, 0, nullptr} /* Sentinel */
};

#if PY_MAJOR_VERSION >= 3
struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_profiler",          /* name of module */
    "Stackdriver Profiler C++ extension module", /* module documentation */
    -1, ProfilerMethods};
}  // namespace

PyMODINIT_FUNC PyInit__profiler(void) { return PyModule_Create(&moduledef); }
#else
}  // namespace

PyMODINIT_FUNC init_profiler(void) {
  Py_InitModule("_profiler", ProfilerMethods);
}
#endif

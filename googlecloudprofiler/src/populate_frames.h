#ifndef THIRD_PARTY_PY_GOOGLECLOUDPROFILER_SRC_POPULATE_FRAMES_H_
#define THIRD_PARTY_PY_GOOGLECLOUDPROFILER_SRC_POPULATE_FRAMES_H_

#include <Python.h>

#include <cstddef>

#include "stacktraces.h"

/**
 * Async-signal-safe read of `n` bytes from `src` (in this process's own address
 * space) into `dst`, via process_vm_readv. Returns false if the source range is
 * not fully readable -- e.g. a torn-down, stale, or otherwise invalid pointer --
 * instead of faulting the process. Used to walk interpreter frames from the
 * SIGPROF handler, and to validate code objects on the collection thread,
 * without dereferencing pointers that may have been invalidated by a race.
 */
bool SafeCopy(void* dst, const void* src, size_t n);

/**
 * Populates the CallFrame array with at-most kMaxFramesToCapture python frames
 * from the provided PyThreadState. Returns the number of frames populated.
 */
int PopulateFrames(CallFrame* frames, PyThreadState* ts);

#endif  // THIRD_PARTY_PY_GOOGLECLOUDPROFILER_SRC_POPULATE_FRAMES_H_

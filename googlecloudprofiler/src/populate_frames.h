#ifndef THIRD_PARTY_PY_GOOGLECLOUDPROFILER_SRC_POPULATE_FRAMES_H_
#define THIRD_PARTY_PY_GOOGLECLOUDPROFILER_SRC_POPULATE_FRAMES_H_

#include <Python.h>

#include "stacktraces.h"

/**
 * Populates the CallFrame array with at-most kMaxFramesToCapture python frames
 * from the provided PyThreadState. Returns the number of frames populated.
 */
int PopulateFrames(CallFrame* frames, PyThreadState* ts);

#endif  // THIRD_PARTY_PY_GOOGLECLOUDPROFILER_SRC_POPULATE_FRAMES_H_

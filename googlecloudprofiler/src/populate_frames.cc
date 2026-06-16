#include "populate_frames.h"

#include <Python.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstddef>

#include "stacktraces.h"

// Python version definitions
#define PY_311 0x030B0000  // 3.11
#define PY_312 0x030C0000  // 3.12
#define PY_313 0x030D0000  // 3.13

// Reads the current process's own memory through the kernel instead of
// dereferencing the pointer directly. process_vm_readv reports an unmapped or
// otherwise invalid source range as EFAULT (a failed return), never a SIGSEGV,
// so the SIGPROF frame walk can follow interpreter pointers that may have been
// torn down or partially written by the interrupted thread without crashing the
// process. Reading our own pid is always permitted and the raw syscall touches
// no libc state, so this is safe to call from the signal handler.
bool SafeCopy(void *dst, const void *src, size_t n) {
  if (src == nullptr) {
    return false;
  }
  struct iovec local = {dst, n};
  struct iovec remote = {const_cast<void *>(src), n};
  long got = syscall(SYS_process_vm_readv, static_cast<long>(getpid()), &local,
                     1UL, &remote, 1UL, 0UL);
  return got == static_cast<long>(n);
}

#if PY_VERSION_HEX >= PY_313

/**
 * Python 3.13 introduced significant changes to the frame structure:
 * - f_code renamed to f_executable (now PyObject* instead of PyCodeObject*)
 * - prev_instr renamed to instr_ptr
 * - cframe->current_frame flattened to tstate->current_frame
 *
 * The PyFrameObject structure members have been removed from the public C API
 * in 3.11:
 * https://docs.python.org/3/whatsnew/3.11.html#pyframeobject-3-11-hiding.
 *
 * The walk runs in the SIGPROF handler, which can interrupt the interpreter at
 * any instruction -- including while a frame or its code object is being set up
 * or torn down. Rather than dereferencing the chain (which faults on a stale or
 * half-written pointer), every interpreter pointer is read with SafeCopy and
 * the walk operates only on the local copies.
 */

#define Py_BUILD_CORE
#include "internal/pycore_frame.h"
#undef Py_BUILD_CORE

// Reads and validates the code object behind a 3.13 frame's f_executable into
// *code_copy. Returns the (live) code pointer, or nullptr if the executable is
// unreadable or is not a code object (f_executable may hold other types).
static PyCodeObject *FrameCode(const _PyInterpreterFrame *fr,
                               PyCodeObject *code_copy) {
  if (fr->f_executable == nullptr ||
      !SafeCopy(code_copy, fr->f_executable, sizeof(*code_copy)) ||
      Py_TYPE(reinterpret_cast<PyObject *>(code_copy)) != &PyCode_Type) {
    return nullptr;
  }
  return reinterpret_cast<PyCodeObject *>(fr->f_executable);
}

// CPython 3.13 _PyFrame_IsIncomplete, reimplemented over copies.
static bool FrameIsIncomplete(const _PyInterpreterFrame *fr, PyCodeObject *code,
                              const PyCodeObject *code_copy) {
  if (fr->owner == FRAME_OWNED_BY_CSTACK) {
    return true;
  }
  if (fr->owner == FRAME_OWNED_BY_GENERATOR) {
    return false;
  }
  if (code == nullptr) {
    return true;
  }
  return fr->instr_ptr < _PyCode_CODE(code) + code_copy->_co_firsttraceable;
}

int PopulateFrames(CallFrame *frames, PyThreadState *ts) {
  if (ts == nullptr) {
    frames[0].lineno = kNoPyState;
    frames[0].py_code = nullptr;
    return 1;
  }

  _PyInterpreterFrame *faddr = ts->current_frame;
  int num_frames = 0;
  while (faddr != nullptr && num_frames < kMaxFramesToCapture) {
    _PyInterpreterFrame fr;
    if (!SafeCopy(&fr, faddr, sizeof(fr))) {
      break;  // unreadable frame: stop, keep the frames gathered so far
    }
    PyCodeObject code_copy;
    PyCodeObject *code = FrameCode(&fr, &code_copy);
    if (code != nullptr && !FrameIsIncomplete(&fr, code, &code_copy)) {
      // Defer line and name/filename resolution to PythonTraces (GIL held).
      // lineno temporarily carries the instruction byte offset; PythonTraces
      // turns it into a source line with PyCode_Addr2Line on the live object.
      frames[num_frames].py_code = code;
      frames[num_frames].lineno = static_cast<int>(
          (fr.instr_ptr - _PyCode_CODE(code)) * sizeof(_Py_CODEUNIT));
      num_frames++;
    }
    faddr = fr.previous;
  }
  return num_frames;
}

#elif PY_VERSION_HEX >= PY_312

/**
 * Python 3.12 changes to the frame structure:
 * - f_code moved to first position in the struct
 * - f_func renamed to f_funcobj
 * - is_entry field removed, return_offset field added
 *
 * The PyFrameObject structure members have been removed from the public C API
 * in 3.11:
 * https://docs.python.org/3/whatsnew/3.11.html#pyframeobject-3-11-hiding.
 *
 * The walk runs in the SIGPROF handler, which can interrupt the interpreter at
 * any instruction -- including while a frame or its code object is being set up
 * or torn down. Rather than dereferencing the chain (which faults on a stale or
 * half-written pointer), every interpreter pointer is read with SafeCopy and
 * the walk operates only on the local copies.
 */

#define Py_BUILD_CORE
#include "internal/pycore_frame.h"
#undef Py_BUILD_CORE

// CPython 3.12 _PyFrame_IsIncomplete, reimplemented over a frame copy so the
// prologue check reads the code object via SafeCopy instead of dereferencing a
// possibly-invalid pointer.
static bool FrameIsIncomplete(const _PyInterpreterFrame *fr) {
  if (fr->owner == FRAME_OWNED_BY_CSTACK) {
    return true;
  }
  if (fr->owner == FRAME_OWNED_BY_GENERATOR) {
    return false;
  }
  PyCodeObject code;
  if (!SafeCopy(&code, fr->f_code, sizeof(code))) {
    return true;  // unreadable code object: treat as incomplete (skip)
  }
  return fr->prev_instr < _PyCode_CODE(fr->f_code) + code._co_firsttraceable;
}

int PopulateFrames(CallFrame *frames, PyThreadState *ts) {
  if (ts == nullptr) {
    frames[0].lineno = kNoPyState;
    frames[0].py_code = nullptr;
    return 1;
  }

  // ts is the live thread state and safe to read directly. current_frame and
  // the frame chain it links, however, can be torn/stale/unmapped if SIGPROF
  // lands mid frame setup or teardown, so each is read with SafeCopy: a bad
  // pointer aborts the walk instead of faulting.
  _PyCFrame *cframe = ts->cframe;
  if (cframe == nullptr) {
    return 0;
  }
  _PyInterpreterFrame *faddr = nullptr;
  if (!SafeCopy(&faddr, &cframe->current_frame, sizeof(faddr))) {
    return 0;
  }

  int num_frames = 0;
  while (faddr != nullptr && num_frames < kMaxFramesToCapture) {
    _PyInterpreterFrame fr;
    if (!SafeCopy(&fr, faddr, sizeof(fr))) {
      break;  // unreadable frame: stop, keep the frames gathered so far
    }
    if (fr.f_code != nullptr && !FrameIsIncomplete(&fr)) {
      // Defer line and name/filename resolution to PythonTraces (GIL held).
      // lineno temporarily carries the instruction byte offset; PythonTraces
      // turns it into a source line with PyCode_Addr2Line on the live object.
      frames[num_frames].py_code = fr.f_code;
      frames[num_frames].lineno = static_cast<int>(
          (fr.prev_instr - _PyCode_CODE(fr.f_code)) * sizeof(_Py_CODEUNIT));
      num_frames++;
    }
    faddr = fr.previous;
  }
  return num_frames;
}

#elif PY_VERSION_HEX >= PY_311

/**
 * Python 3.11 frame structure baseline.
 *
 * The PyFrameObject structure members have been removed from the public C API
 * in 3.11:
 * https://docs.python.org/3/whatsnew/3.11.html#pyframeobject-3-11-hiding.
 *
 * The walk runs in the SIGPROF handler, which can interrupt the interpreter at
 * any instruction -- including while a frame or its code object is being set up
 * or torn down. Rather than dereferencing the chain (which faults on a stale or
 * half-written pointer), every interpreter pointer is read with SafeCopy and
 * the walk operates only on the local copies.
 */

#define Py_BUILD_CORE
#include "internal/pycore_frame.h"
#undef Py_BUILD_CORE

// CPython 3.11 _PyFrame_IsIncomplete, reimplemented over a frame copy so the
// prologue check reads the code object via SafeCopy instead of dereferencing a
// possibly-invalid pointer. (3.11 has no FRAME_OWNED_BY_CSTACK.)
static bool FrameIsIncomplete(const _PyInterpreterFrame *fr) {
  if (fr->owner == FRAME_OWNED_BY_GENERATOR) {
    return false;
  }
  PyCodeObject code;
  if (!SafeCopy(&code, fr->f_code, sizeof(code))) {
    return true;  // unreadable code object: treat as incomplete (skip)
  }
  return fr->prev_instr < _PyCode_CODE(fr->f_code) + code._co_firsttraceable;
}

int PopulateFrames(CallFrame *frames, PyThreadState *ts) {
  if (ts == nullptr) {
    frames[0].lineno = kNoPyState;
    frames[0].py_code = nullptr;
    return 1;
  }

  // ts is the live thread state and safe to read directly. current_frame and
  // the frame chain it links, however, can be torn/stale/unmapped if SIGPROF
  // lands mid frame setup or teardown, so each is read with SafeCopy: a bad
  // pointer aborts the walk instead of faulting.
  _PyCFrame *cframe = ts->cframe;
  if (cframe == nullptr) {
    return 0;
  }
  _PyInterpreterFrame *faddr = nullptr;
  if (!SafeCopy(&faddr, &cframe->current_frame, sizeof(faddr))) {
    return 0;
  }

  int num_frames = 0;
  while (faddr != nullptr && num_frames < kMaxFramesToCapture) {
    _PyInterpreterFrame fr;
    if (!SafeCopy(&fr, faddr, sizeof(fr))) {
      break;  // unreadable frame: stop, keep the frames gathered so far
    }
    if (fr.f_code != nullptr && !FrameIsIncomplete(&fr)) {
      // Defer line and name/filename resolution to PythonTraces (GIL held).
      // lineno temporarily carries the instruction byte offset; PythonTraces
      // turns it into a source line with PyCode_Addr2Line on the live object.
      frames[num_frames].py_code = fr.f_code;
      frames[num_frames].lineno = static_cast<int>(
          (fr.prev_instr - _PyCode_CODE(fr.f_code)) * sizeof(_Py_CODEUNIT));
      num_frames++;
    }
    faddr = fr.previous;
  }
  return num_frames;
}

#else
// python versions before 3.11

int PopulateFrames(CallFrame *frames, PyThreadState *ts) {
  if (ts == nullptr) {
    frames[0].lineno = kNoPyState;
    frames[0].py_code = nullptr;
    return 1;
  }
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
  return num_frames;
}

#endif  // PY_VERSION_HEX >= PY_311

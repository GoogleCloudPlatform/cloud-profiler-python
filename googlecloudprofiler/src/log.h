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

#ifndef GOOGLECLOUDPROFILER_SRC_LOG_H_
#define GOOGLECLOUDPROFILER_SRC_LOG_H_

// Logs the error message using Python logging.error. It accepts arguments
// like printf: format specifiers in the given fmt are replaced by the
// corresponding additional arguments.
void LogError(const char *fmt, ...);

// Logs the warning message using Python logging.warning. It accepts arguments
// like printf: format specifiers in the given fmt are replaced by the
// corresponding additional arguments.
void LogWarning(const char *fmt, ...);

// Logs the info message using Python logging.info. It accepts arguments
// like printf: format specifiers in the given fmt are replaced by the
// corresponding additional arguments.
void LogInfo(const char *fmt, ...);

// Logs the debug message using Python logging.debug. It accepts arguments
// like printf: format specifiers in the given fmt are replaced by the
// corresponding additional arguments.
void LogDebug(const char *fmt, ...);

#endif  // GOOGLECLOUDPROFILER_SRC_LOG_H_

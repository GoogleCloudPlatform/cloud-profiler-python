#!/bin/bash
#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

retry() {
  for i in {1..3}; do
    "${@}" && return 0
  done
  return 1
}

# Fail on any error.
set -eo pipefail

# Display commands being run.
set -x

cd $(dirname $0)/..

export GCLOUD_TESTS_PYTHON_PROJECT_ID="cloud-profiler-e2e"

export GCLOUD_TESTS_PYTHON_ZONE="us-west2-a"

export GOOGLE_APPLICATION_CREDENTIALS="${KOKORO_KEYSTORE_DIR}/72935_cloud-profiler-e2e-service-account-key"

# Package the agent and upload to GCS.
retry python3 -m pip install --user --upgrade setuptools wheel twine
python3 setup.py sdist
AGENT_PATH=$(find "$PWD/dist" -name "google-cloud-profiler*")
GCS_LOCATION="cprof-e2e-artifacts/python/kokoro/${KOKORO_JOB_TYPE}/${KOKORO_BUILD_NUMBER}"
retry gcloud auth activate-service-account --key-file="${GOOGLE_APPLICATION_CREDENTIALS}"
retry gsutil cp "${AGENT_PATH}" "gs://${GCS_LOCATION}/"

# Move test to go path.
export GOPATH="$HOME/go"
mkdir -p "$GOPATH/src"
cp -R "kokoro" "$GOPATH/src/proftest"

# Run test.
cd "$GOPATH/src/proftest"
retry go get -t -d -u .
go test -timeout=30m -run TestAgentIntegration -gcs_location="${GCS_LOCATION}"



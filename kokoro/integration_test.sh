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
     [[ $i == 1 ]] || sleep 10  # Backing off after a failed attempt.
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

export GCLOUD_TESTS_PYTHON_ZONE="us-west3-b"

export GOOGLE_APPLICATION_CREDENTIALS="${KOKORO_KEYSTORE_DIR}/72935_cloud-profiler-e2e-service-account-key"

# Package the agent and upload to GCS.
retry python3 -m pip install --user --upgrade setuptools wheel twine
python3 setup.py sdist
AGENT_PATH=$(find "$PWD/dist" -name "google-cloud-profiler*")
GCS_LOCATION="cprof-e2e-artifacts/python/kokoro/${KOKORO_JOB_TYPE}/${KOKORO_BUILD_NUMBER}"
retry gcloud auth activate-service-account --key-file="${GOOGLE_APPLICATION_CREDENTIALS}"
retry gsutil cp "${AGENT_PATH}" "gs://${GCS_LOCATION}/"

# Run test.
cd "kokoro"

# Backoff test should not be run on presubmit.
RUN_BACKOFF_TEST="true"
if [[ "$KOKORO_JOB_TYPE" == "PRESUBMIT_GITHUB" ]]; then
  RUN_BACKOFF_TEST="false"
fi

go version

# The current Go version in the VM is 1.18.4, however we explicitly set it to
# pin the Go dependency to v1.18.4 for consistency.
retry curl -LO https://go.dev/dl/go1.18.4.linux-amd64.tar.gz
sudo rm -rf /usr/local/go && tar -C /usr/local -xzf go1.18.4.linux-amd64.tar.gz
export PATH=$PATH:/usr/local/go/bin

# Initializing go modules allows our dependencies to install versions of their
# dependencies specified by their go.mod files. This reduces the likelihood of
# dependencies breaking this test.
go version
go mod init e2e

# Compile test before running to download dependencies.
retry go get cloud.google.com/go/profiler/proftest@HEAD
retry go test -c
./e2e.test  -gcs_location="${GCS_LOCATION}" -run_backoff_test=$RUN_BACKOFF_TEST

# Exit with success code if no need to release the agent.
if [[ "$KOKORO_JOB_TYPE" != "RELEASE" ]]; then
  exit 0
fi

# Release the agent to PyPI.
PYPI_PASSWORD="$(cat "$KOKORO_KEYSTORE_DIR"/72935_pypi-google-cloud-profiler-team-password)"
cat >~/.pypirc <<EOF
[distutils]
index-servers =
   pypi

[pypi]
username:google-cloud-profiler-team
password:${PYPI_PASSWORD}

EOF

python3 -m twine upload "${AGENT_PATH}"

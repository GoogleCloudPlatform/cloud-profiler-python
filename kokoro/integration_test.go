// Copyright 2019 Google LLC
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

package e2e

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"runtime"
	"strings"
	"testing"
	"text/template"
	"time"

	"cloud.google.com/go/profiler/proftest"
	"golang.org/x/net/context"
	"golang.org/x/oauth2/google"

	compute "google.golang.org/api/compute/v1"
)

var (
	gcsLocation       = flag.String("gcs_location", "", "GCS location for the agent")
	runBackoffTest    = flag.Bool("run_backoff_test", false, "Enables the backoff integration test. This integration test requires over 45 mins to run, so it is not run by default.")
	runID             = strings.ToLower(strings.Replace(time.Now().Format("2006-01-02-15-04-05.000000-MST"), ".", "-", -1))
	benchFinishString = "benchmark application(s) complete"
	errorString       = "failed to set up or run the benchmark"
)

const (
	cloudScope       = "https://www.googleapis.com/auth/cloud-platform"
	storageReadScope = "https://www.googleapis.com/auth/devstorage.read_only"
	defaultGetPipURL = "https://bootstrap.pypa.io/get-pip.py"

	gceBenchDuration = 600 * time.Second
	gceTestTimeout   = 20 * time.Minute

	// For any agents to receive backoff, there must be more than 32 agents in
	// the deployment. The initial backoff received will be 33 minutes; each
	// subsequent backoff will be one minute longer. Running 45 benchmarks for
	// 45 minutes will ensure that several agents receive backoff responses and
	// are able to wait for the backoff duration then send another request.
	numBackoffBenchmarks = 45
	backoffBenchDuration = 45 * time.Minute
	backoffTestTimeout   = 60 * time.Minute
)

const startupTemplate = `
{{ define "setup"}}

# Install dependencies.
{{if .InstallPythonVersion}}
# ppa:deadsnakes/ppa contains desired Python versions.
retry add-apt-repository -y ppa:deadsnakes/ppa >/dev/null
{{end}}
# Force IPv4 to prevent long IPv6 timeouts.
# TODO : Validate this solves the issue. Remove if not.
retry apt-get -o Acquire::ForceIPv4=true update >/dev/null
retry apt-get -o Acquire::ForceIPv4=true install -yq git build-essential python3-distutils {{.PythonDev}} {{if .InstallPythonVersion}}{{.InstallPythonVersion}}{{end}} >/dev/ttyS2
# Print current Python version.
{{.PythonCommand}} --version
# Distutils need to be installed separately when explicitly testing various
# Python versions.
{{if .InstallPythonVersion}}
retry apt-get -yq install {{.InstallPythonVersion}}-distutils
{{end}}

# Install Python dependencies.
retry wget -O /tmp/get-pip.py {{.GetPipURL}} >/dev/null
retry {{.PythonCommand}} /tmp/get-pip.py >/dev/null
retry {{.PythonCommand}} -m pip install --upgrade pyasn1 >/dev/null

# Setup pipenv
retry {{.PythonCommand}} -m pip install pipenv > /dev/null
mkdir bench && cd bench
retry pipenv install > /dev/null


# Fetch agent.
mkdir /tmp/agent
retry gsutil cp gs://{{.GCSLocation}}/* /tmp/agent

# Install agent.
retry pipenv run {{.PythonCommand}} -m pip install --ignore-installed "$(find /tmp/agent -name "google_cloud_profiler*")"

# Write bench app.
export BENCH_DIR="$HOME/bench"

cat << EOF > bench.py
import googlecloudprofiler
import sys
import time
import traceback

def python_bench():
  for counter in range(1, 5000):
    pass

def repeat_bench(dur_sec):
  t_end = time.time() + dur_sec
  while time.time() < t_end or dur_sec == 0:
    python_bench()

if __name__ == '__main__':
  if not {{.VersionCheck}}:
    raise EnvironmentError('Python version %s failed to satisfy "{{.VersionCheck}}".' % str(sys.version_info))

  try:
    googlecloudprofiler.start(
      service='{{.Service}}',
      service_version='1.0.0',
      verbose=3)
  except BaseException:
    sys.exit('Failed to start the profiler: %s' % traceback.format_exc())
  repeat_bench({{.DurationSec}})
EOF

{{- end }}

{{ define "integration" -}}
{{- template "prologue" . }}
{{- template "setup" . }}

# Run bench app.
pipenv run {{.PythonCommand}} bench.py

# Indicate to test that script has finished running.
echo "{{.FinishString}}"

{{ template "epilogue" . -}}
{{end}}

{{ define "integration_backoff" -}}
{{- template "prologue" . }}
{{- template "setup" . }}

# Do not display commands being run to simplify logging output.
set +x

echo "Starting {{.NumBackoffBenchmarks}} benchmarks."
for (( i = 0; i < {{.NumBackoffBenchmarks}}; i++ )); do
	(pipenv run {{.PythonCommand}} bench.py) |& while read line; \
	     do echo "benchmark $i: ${line}"; done &
done
echo "Successfully started {{.NumBackoffBenchmarks}} benchmarks."

wait

# Continue displaying commands being run.
set -x

echo "{{.FinishString}}"

{{ template "epilogue" . -}}
{{ end }}
`

type testCase struct {
	proftest.InstanceConfig
	name string
	// Python version to install. Empty string means no installation is needed.
	installPythonVersion string
	// Python command name, e.g "python" or "python3".
	pythonCommand string
	// The python-dev package to install, e.g "python-dev" or "python3.5-dev".
	pythonDev string
	// Used in the bench code to check the Python version, e.g
	// "sys.version_info[:2] == (2.7)".
	versionCheck string
	// URL of the get-pip.py script, defaults to
	// the value of https://bootstrap.pypa.io/get-pip.py when not specified.
	getPipURL string
	// Timeout for the integration test.
	timeout time.Duration
	// When true, a backoff test should be run. Otherwise, run a standard
	// integration test.
	backoffTest bool
	// Duration for which benchmark application should run.
	benchDuration time.Duration
	// Maps profile type to function name wanted for that type. Empty function
	// name means the type should not be profiled. Only used when backoffTest is
	// false.
	wantProfiles map[string]string
}

func (tc *testCase) initializeStartUpScript(template *template.Template) error {
	params := struct {
		Service              string
		GCSLocation          string
		InstallPythonVersion string
		PythonCommand        string
		PythonDev            string
		VersionCheck         string
		GetPipURL            string
		FinishString         string
		ErrorString          string
		DurationSec          int
		NumBackoffBenchmarks int
	}{
		Service:              tc.name,
		GCSLocation:          *gcsLocation,
		InstallPythonVersion: tc.installPythonVersion,
		PythonCommand:        tc.pythonCommand,
		PythonDev:            tc.pythonDev,
		VersionCheck:         tc.versionCheck,
		GetPipURL:            tc.getPipURL,
		FinishString:         benchFinishString,
		ErrorString:          errorString,
		DurationSec:          int(tc.benchDuration.Seconds()),
	}

	testTemplate := "integration"
	if tc.backoffTest {
		testTemplate = "integration_backoff"
		params.NumBackoffBenchmarks = numBackoffBenchmarks
	}

	var buf bytes.Buffer
	err := template.Lookup(testTemplate).Execute(&buf, params)
	if err != nil {
		return fmt.Errorf("failed to render startup script for %s: %v", tc.name, err)
	}
	tc.StartupScript = buf.String()
	return nil
}

func TestAgentIntegration(t *testing.T) {
	projectID := os.Getenv("GCLOUD_TESTS_PYTHON_PROJECT_ID")
	if projectID == "" {
		t.Fatalf("Getenv(GCLOUD_TESTS_PYTHON_PROJECT_ID) got empty string")
	}

	zone := os.Getenv("GCLOUD_TESTS_PYTHON_ZONE")
	if zone == "" {
		t.Fatalf("Getenv(GCLOUD_TESTS_PYTHON_ZONE) got empty string")
	}

	if *gcsLocation == "" {
		t.Fatal("gcsLocation flag is not set")
	}

	ctx := context.Background()

	client, err := google.DefaultClient(ctx, cloudScope)
	if err != nil {
		t.Fatalf("failed to get default client: %v", err)
	}

	computeService, err := compute.New(client)
	if err != nil {
		t.Fatalf("failed to initialize compute Service: %v", err)
	}
	template, err := proftest.BaseStartupTmpl.Parse(startupTemplate)
	if err != nil {
		t.Fatalf("failed to parse startup script template: %v", err)
	}

	gceTr := proftest.GCETestRunner{
		TestRunner: proftest.TestRunner{
			Client: client,
		},
		ComputeService: computeService,
	}

	testcases := generateTestCases(projectID, zone)

	// Allow test cases to run in parallel.
	runtime.GOMAXPROCS(len(testcases))

	for _, tc := range testcases {
		tc := tc // capture range variable
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			if err := tc.initializeStartUpScript(template); err != nil {
				t.Fatalf("failed to initialize startup script: %v", err)
			}

			gceTr.StartInstance(ctx, &tc.InstanceConfig)
			defer func() {
				if gceTr.DeleteInstance(ctx, &tc.InstanceConfig); err != nil {
					t.Fatalf("failed to delete instance: %v", err)
				}
			}()

			timeoutCtx, cancel := context.WithTimeout(ctx, tc.timeout)
			defer cancel()
			output, err := gceTr.PollAndLogSerialPort(timeoutCtx, &tc.InstanceConfig, benchFinishString, errorString, t.Logf)
			if err != nil {
				t.Fatal(err)
			}

			if tc.backoffTest {
				if err := proftest.CheckSerialOutputForBackoffs(output, numBackoffBenchmarks, "generic::aborted: action throttled, backoff for", "Starting to create profile", "benchmark"); err != nil {
					t.Errorf("failed to check serial output for backoffs: %v", err)
				}
				return
			}

			timeNow := time.Now()
			endTime := timeNow.Format(time.RFC3339)
			startTime := timeNow.Add(-1 * time.Hour).Format(time.RFC3339)
			for pType, function := range tc.wantProfiles {
				pr, err := gceTr.TestRunner.QueryProfilesWithZone(tc.ProjectID, tc.name, startTime, endTime, pType, zone)
				if function == "" {
					if err == nil {
						t.Errorf("QueryProfilesWithZone(%s, %s, %s, %s, %s, %s) got profile, want no profile", tc.ProjectID, tc.name, startTime, endTime, pType, zone)
					}
					continue
				}

				if err != nil {
					t.Errorf("QueryProfiles(%s, %s, %s, %s, %s) got error: %v", tc.ProjectID, tc.name, startTime, endTime, pType, err)
					continue
				}

				if err := pr.HasFunction(function); err != nil {
					t.Errorf("Function %s not found in profiles of type %s: %v", function, pType, err)
				}
			}
		})
	}
}

func generateTestCases(projectID, zone string) []testCase {
	tcs := []testCase{
		// Test GCE Ubuntu default Python 3, expect Python 3.10.
		{
			InstanceConfig: proftest.InstanceConfig{
				ProjectID:    projectID,
				Zone:         zone,
				Name:         fmt.Sprintf("profiler-test-python3-%s", runID),
				MachineType:  "n1-standard-1",
				ImageProject: "ubuntu-os-cloud",
				ImageFamily:  "ubuntu-2204-lts",
				Scopes:       []string{storageReadScope},
			},
			name: fmt.Sprintf("profiler-test-python3-%s-gce", runID),
			wantProfiles: map[string]string{
				"WALL": "repeat_bench",
				"CPU":  "repeat_bench",
			},
			pythonCommand: "python3",
			pythonDev:     "python3-dev",
			versionCheck:  "sys.version_info[:2] == (3, 10)",
			getPipURL:     defaultGetPipURL,
			timeout:       gceTestTimeout,
			benchDuration: gceBenchDuration,
		},
	}

	for _, minorVersion := range []int{7, 8, 9, 10, 11} {
		getPipURL := defaultGetPipURL
		// TODO: remove special case once 3.7 is dropped
		if minorVersion == 7 {
			getPipURL = "https://bootstrap.pypa.io/pip/3.7/get-pip.py"
		}

		tcs = append(tcs, testCase{
			InstanceConfig: proftest.InstanceConfig{
				ProjectID:    projectID,
				Zone:         zone,
				Name:         fmt.Sprintf("profiler-test-python3%d-%s", minorVersion, runID),
				MachineType:  "n1-standard-1",
				ImageProject: "ubuntu-os-cloud",
				ImageFamily:  "ubuntu-2204-lts",
				Scopes:       []string{storageReadScope},
			},
			name: fmt.Sprintf("profiler-test-python3%d-%s-gce", minorVersion, runID),
			wantProfiles: map[string]string{
				"WALL": "repeat_bench",
				"CPU":  "repeat_bench",
			},
			installPythonVersion: fmt.Sprintf("python3.%d", minorVersion),
			pythonCommand:        fmt.Sprintf("python3.%d", minorVersion),
			getPipURL:            getPipURL,
			pythonDev:            fmt.Sprintf("python3.%d-dev", minorVersion),
			versionCheck:         fmt.Sprintf("sys.version_info[:2] >= (3, %d)", minorVersion),
			timeout:              gceTestTimeout,
			benchDuration:        gceBenchDuration,
		})
	}

	if *runBackoffTest {
		tcs = append(tcs, testCase{
			// Test GCE Ubuntu default Python 3, expect Python 3.10.
			InstanceConfig: proftest.InstanceConfig{
				ProjectID:    projectID,
				Zone:         zone,
				Name:         fmt.Sprintf("profiler-test-python3-backoff-%s", runID),
				ImageProject: "ubuntu-os-cloud",
				ImageFamily:  "ubuntu-2204-lts",
				Scopes:       []string{storageReadScope},

				// Running many copies of the benchmark requires more
				// memory than is available on an n1-standard-1. Use a
				// machine type with more memory for backoff test.
				MachineType: "n1-highmem-2",
			},
			name: fmt.Sprintf("profiler-test-python3-backoff-%s-gce", runID),
			wantProfiles: map[string]string{
				"WALL": "repeat_bench",
				"CPU":  "repeat_bench",
			},
			pythonCommand: "python3",
			pythonDev:     "python3-dev",
			getPipURL:     defaultGetPipURL,
			versionCheck:  "sys.version_info[:2] == (3, 10)",
			timeout:       backoffTestTimeout,
			benchDuration: backoffBenchDuration,
			backoffTest:   true,
		})
	}

	return tcs
}

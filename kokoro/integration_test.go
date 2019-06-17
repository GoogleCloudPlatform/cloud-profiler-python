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

package testing

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
	repo              = flag.String("repo", "https://github.com/GoogleCloudPlatform/cloud-profiler-python.git", "git repo to test")
	branch            = flag.String("branch", "", "git branch to test")
	commit            = flag.String("commit", "", "git commit to test")
	pr                = flag.Int("pr", 0, "git pull request to test")
	runID             = strings.Replace(time.Now().Format("2006-01-02-15-04-05.000000-0700"), ".", "-", -1)
	benchFinishString = "busybench finished profiling"
	errorString       = "failed to set up or run the benchmark"
)

const cloudScope = "https://www.googleapis.com/auth/cloud-platform"

const startupTemplate = `
{{- template "prologue" . }}

# Install dependencies.
retry apt-get update >/dev/null
retry apt-get install -yq git build-essential python-dev python3-dev >/dev/null

# Install desired version of Python.
{{if .InstallPythonVersion}}
retry add-apt-repository ppa:deadsnakes/ppa >/dev/null
retry apt-get update >/dev/null
retry apt-get install {{.InstallPythonVersion}} >/dev/null
{{end}}

# Install Python dependencies.
retry wget -O /tmp/get-pip.py https://bootstrap.pypa.io/get-pip.py >/dev/null
retry {{.PythonCommand}} /tmp/get-pip.py >/dev/null
retry {{.PythonCommand}} -m pip install --upgrade pyasn1 >/dev/null

# Fetch agent.
retry git clone {{.Repo}}
cd cloud-profiler-python
retry git fetch origin {{if .PR}}pull/{{.PR}}/head{{else}}{{.Branch}}{{end}}:pull_branch
git checkout pull_branch
git reset --hard {{.Commit}}

# Install agent.
{{.PythonCommand}} setup.py sdist
cd dist/
{{.PythonCommand}} -m pip install "$(basename -- $(find . -name "google-cloud-profiler*"))"

# Run bench app.
export BENCH_DIR="$HOME/bench"
mkdir -p $BENCH_DIR
cd $BENCH_DIR

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
  repeat_bench(3 * 60)
EOF

# TODO(b/133360821): Stop ignoring exit code SIGALRM when b/133360821 is fixed.
{{.PythonCommand}} bench.py || [ "$?" -eq "142" ]

# Indicate to test that script has finished running.
echo "{{.FinishString}}"

{{ template "epilogue" . -}}
`

type testCase struct {
	proftest.InstanceConfig
	name string
	// Python version to install. Empty string means no installation is needed.
	installPythonVersion string
	// Python command name, e.g "python" or "python3".
	pythonCommand string
	// Used in the bench code to check the Python version, e.g
	// "sys.version_info[:2] == (2.7)".
	versionCheck string
	// Maps profile type to function name wanted for that type. Empty function
	// name means the type should not be profiled.
	wantProfiles map[string]string
}

func (tc *testCase) initializeStartUpScript(template *template.Template) error {
	var buf bytes.Buffer
	err := template.Execute(&buf,
		struct {
			Service              string
			InstallPythonVersion string
			PythonCommand        string
			VersionCheck         string
			Repo                 string
			PR                   int
			Branch               string
			Commit               string
			FinishString         string
			ErrorString          string
		}{
			Service:              tc.name,
			InstallPythonVersion: tc.installPythonVersion,
			PythonCommand:        tc.pythonCommand,
			VersionCheck:         tc.versionCheck,
			Repo:                 *repo,
			PR:                   *pr,
			Branch:               *branch,
			Commit:               *commit,
			FinishString:         benchFinishString,
			ErrorString:          errorString,
		})
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

	if *commit == "" {
		t.Fatal("commit flag is not set")
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

	testcases := []testCase{
		// Test GCE Ubuntu default Python 2, should be Python 2.7.
		{
			InstanceConfig: proftest.InstanceConfig{
				ProjectID:    projectID,
				Zone:         zone,
				Name:         fmt.Sprintf("profiler-test-python2-%s", runID),
				MachineType:  "n1-standard-1",
				ImageProject: "ubuntu-os-cloud",
				ImageFamily:  "ubuntu-1804-lts",
			},
			name: fmt.Sprintf("profiler-test-python2-%s-gce", runID),
			wantProfiles: map[string]string{
				"WALL": "repeat_bench",
				// CPU profiling should be disabled on Python 2.
				"CPU": "",
			},
			pythonCommand: "python2.7",
			versionCheck:  "sys.version_info[:2] == (2, 7)",
		},
		// Test GCE Ubuntu default Python 3, should be Python 3.6 or higher.
		{
			InstanceConfig: proftest.InstanceConfig{
				ProjectID:    projectID,
				Zone:         zone,
				Name:         fmt.Sprintf("profiler-test-python3-%s", runID),
				MachineType:  "n1-standard-1",
				ImageProject: "ubuntu-os-cloud",
				ImageFamily:  "ubuntu-1804-lts",
			},
			name: fmt.Sprintf("profiler-test-python3-%s-gce", runID),
			wantProfiles: map[string]string{
				"WALL": "repeat_bench",
				"CPU":  "repeat_bench",
			},
			pythonCommand: "python3",
			versionCheck:  "sys.version_info[:2] >= (3, 6)",
		},
		// Test Python 3.5.
		{
			InstanceConfig: proftest.InstanceConfig{
				ProjectID:    projectID,
				Zone:         zone,
				Name:         fmt.Sprintf("profiler-test-python35-%s", runID),
				MachineType:  "n1-standard-1",
				ImageProject: "ubuntu-os-cloud",
				// ppa:deadsnakes/ppa is not yet available on Ubuntu 18.10.
				ImageFamily: "ubuntu-1604-lts",
			},
			name: fmt.Sprintf("profiler-test-python35-%s-gce", runID),
			wantProfiles: map[string]string{
				"CPU": "repeat_bench",
				// Wall profiling should be disabled on Python 3.5.
				"WALL": "",
			},
			installPythonVersion: "python3.5",
			pythonCommand:        "python3.5",
			versionCheck:         "sys.version_info[:2] == (3, 5)",
		},
	}

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

			timeoutCtx, cancel := context.WithTimeout(ctx, time.Minute*20)
			defer cancel()
			if err := gceTr.PollForSerialOutput(timeoutCtx, &tc.InstanceConfig, benchFinishString, errorString); err != nil {
				t.Fatal(err)
			}

			timeNow := time.Now()
			endTime := timeNow.Format(time.RFC3339)
			startTime := timeNow.Add(-1 * time.Hour).Format(time.RFC3339)
			for pType, function := range tc.wantProfiles {
				pr, err := gceTr.TestRunner.QueryProfiles(tc.ProjectID, tc.name, startTime, endTime, pType)
				if function == "" {
					if err == nil {
						t.Errorf("QueryProfiles(%s, %s, %s, %s, %s) got profile, want no profile", tc.ProjectID, tc.name, startTime, endTime, pType)
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

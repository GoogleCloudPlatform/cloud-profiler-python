# Stackdriver Python profiling agent

Python profiling agent for
[Stackdriver Profiler](https://cloud.google.com/profiler/).

## Installation & usage

1.  Install the profiler package using PyPI:

    ```shell
    pip3 install google-cloud-profiler
    ```

2.  Enable the profiler in your application:

    ```python
    import googlecloudprofiler

    def main():
        # Profiler initialization. It starts a daemon thread which continuously
        # collects and uploads profiles. Best done as early as possible.
        try:
            googlecloudprofiler.start(
                service='hello-profiler',
                service_version='1.0.1',
                # verbose is the logging level. 0-error, 1-warning, 2-info,
                # 3-debug. It defaults to 0 (error) if not set.
                verbose=3,
                # project_id must be set if not running on GCP.
                # project_id='my-project-id',
            )
        except (ValueError, NotImplementedError) as exc:
            print(exc)  # Handle errors here
    ```

See
[Stackdriver Profiler profiling Python code](https://cloud.google.com/profiler/docs/profiling-python)
for detailed documentation.

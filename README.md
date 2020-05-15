# Google Cloud Python profiling agent

Python profiling agent for
[Google Cloud Profiler](https://cloud.google.com/profiler/).

See
[Google Cloud Profiler profiling Python code](https://cloud.google.com/profiler/docs/profiling-python)
for detailed documentation.

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

## Installation on Linux Alpine

The Python profiling agent has a native component. The base Alpine image for
Python does not have all dependencies required to build this native component
installed. To build the Python profiling agent on Alpine, one must install the
package `build-base`.

To use the Python profiling agent on Alpine without installing additional
dependencies on to the final Alpine image, one can use a two-stage build and
compile the Python profiling agent in the first stage.

Here is an example of a Docker image that uses a multi-stage build to compile
and install the Python profiling agent:

```
FROM python:3.7-alpine as builder

# Install build-base to allow for compilation of the profiling agent.
RUN apk add --update --no-cache build-base

# Compile the profiling agent, generating wheels for it.
RUN pip3 wheel --wheel-dir=/tmp/wheels google-cloud-profiler


FROM python:3.7-alpine

# Copy over the directory containing wheels for the profiling agent.
COPY --from=builder /tmp/wheels /tmp/wheels

# Install the profiling agent.
RUN pip3 install --no-index --find-links=/tmp/wheels google-cloud-profiler

# Install any other required modules or dependencies, and copy an app which
# enables the profiler as described in "Enable the profiler in your
# application".
COPY ./bench.py .

# Run the application when the docker image is run, using either CMD (as is done
# here) or ENTRYPOINT.
CMD python3 -u bench.py
```


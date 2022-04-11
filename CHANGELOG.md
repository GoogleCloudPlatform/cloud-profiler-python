### 3.0.8

### Internal / Testing Changes

*   test: make integration test use Go 1.17.7
    ([6996cf6](https://github.com/GoogleCloudPlatform/cloud-profiler-python/6996cf6eea8ba814abab5ff625ca5a03b09dbc08))

*   test: Use Python 3.6 specific get-pip.py when testing with Python 3.6.
    ([70f93b5](https://github.com/GoogleCloudPlatform/cloud-profiler-python/70f93b53187e074f5fd354a9f1fd19e25de79a6d))

### 3.0.7

### Bug Fixes

*   fix: rollback workaround for certification issue
    ([ca588f5](https://github.com/GoogleCloudPlatform/cloud-profiler-python/ca588f58081258b1259a2564f2a1614c6c949495))

### 3.0.6

### Bug Fixes

*   fix: workaround certificate expiration issue in integration tests
    ([80f423f](https://github.com/GoogleCloudPlatform/cloud-profiler-python/80f423f439cbc780d2da8930abc0b99308378abb))

### Internal / Testing Changes

*   chore: log most errors at warning level.
    ([8147311](https://github.com/GoogleCloudPlatform/cloud-profiler-python/814731125216fd1c332c9d90074635485e8ac62f))

### 3.0.5

### Documentation

*   doc: update the changelog for release of 3.0.4
    ([57b45cf](https://github.com/GoogleCloudPlatform/cloud-profiler-python/57b45cf2a72333063bc64c3dc636098e0571e8cf))

### Internal / Testing Changes

*   test: display environment variables when encountering an error
    ([ad2ce5b](https://github.com/GoogleCloudPlatform/cloud-profiler-python/ad2ce5bad286fd7f1dddee741babb5a374339518))

*   test: temporarily disable testing for Python 3.10 until
    https://github.com/pypa/pip/issues/9951 is resolved
    ([4197241](https://github.com/GoogleCloudPlatform/cloud-profiler-python/41972412bb45e484552bac803bf1319222224415))

*   chore: make CHANGELOG.md a top-level file
    ([058e646](https://github.com/GoogleCloudPlatform/cloud-profiler-python/058e6467a217f48b2155b2f31336fcd4e7fb4030))

### 3.0.4

### Dependencies

*   chore: requires google-api-python-client != 2.0.2 to avoid private API
    incompatibility issue
    ([5738fe8](https://github.com/GoogleCloudPlatform/cloud-profiler-python/5738fe8e2a68dee548c0b4ba9465bfa48d019706))

### Documentation

*   doc: add CHANGELOG.md file
    ([dabfdd6](https://github.com/GoogleCloudPlatform/cloud-profiler-python/dabfdd6cdd8c3a181c4d8dec607a7e907e4fac7e))

### 3.0.3

### Bug Fixes

*   Fix the import issue that breaks the Python agent on macOS.
    ([a254dd6](https://github.com/GoogleCloudPlatform/cloud-profiler-python/a254dd60eb871332789d9b10d0cb97a35e82cbc9))

### 3.0.2

### Internal / Testing Changes

*   Add integration tests to officially support 3.8 and 3.9.
    ([58eeb62](https://github.com/GoogleCloudPlatform/cloud-profiler-python/58eeb622d44919e0ab622dbfa90b3e75888c9b04))

### 3.0.1

### Bug Fixes

*   Use google-api-python-client < 2.0.0 since latest version is not compatible
    with test endpoints.
    ([5f6459a](https://github.com/GoogleCloudPlatform/cloud-profiler-python/5f6459ac968195890bccf918b19959b3f5ed317d))

## 3.0.0

### ⚠ BREAKING CHANGES

*   Drop support for Python version prior to 3.6.
    ([c64557c](https://github.com/GoogleCloudPlatform/cloud-profiler-python/c64557c8c13cf84f38edeb70080c0db6dd3b2bac))

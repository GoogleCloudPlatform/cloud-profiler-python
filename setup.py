# Copyright 2018 Google LLC
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
"""Cloud Profiler Python agent packaging script."""

from __future__ import print_function

import glob
import sys
from setuptools import Extension
from setuptools import setup

install_requires = [
    'google-api-python-client',
    'google-auth>=1.0.0',
    'google-auth-httplib2',
    'protobuf',
    'requests',
]

ext_module = [
    Extension(
        'googlecloudprofiler._profiler',
        sources=glob.glob('googlecloudprofiler/src/*.cc'),
        include_dirs=['googlecloudprofiler/src'],
        language='c++',
        extra_compile_args=['-std=c++11'],
        extra_link_args=['-std=c++11', '-static-libstdc++'])
]

if not (sys.platform.startswith('linux') or sys.platform.startswith('darwin')):
  print(
      sys.platform, 'is not a supported operating system.\n'
      'Profiler Python agent modules will be installed but will not '
      'be functional. Refer to the documentation for a list of '
      'supported operating systems.\n')
  ext_module = []

if sys.platform.startswith('darwin'):
  print(
      'Profiler Python agent has limited support for ', sys.platform, '. '
      'Wall profiler is available with supported Python versions. '
      'CPU profiler is not available. '
      'Refer to the documentation for a list of supported operating '
      'systems and Python versions.\n')
  ext_module = []

setup(
    name='google-cloud-profiler',
    description='Stackdriver Profiler Python Agent',
    long_description=open('README.md').read(),
    long_description_content_type='text/markdown',
    url='https://github.com/GoogleCloudPlatform/cloud-profiler-python',
    author='Google LLC',
    version='1.0.8',
    install_requires=install_requires,
    setup_requires=['wheel'],
    packages=['googlecloudprofiler'],
    ext_modules=ext_module,
    license='Apache License, Version 2.0',
    keywords='google cloud profiler',
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: Apache Software License',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3.2',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
    ])

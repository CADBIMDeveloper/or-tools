# Copyright 2010-2022 Google LLC
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Helper macro to compile and test code samples."""

load("@ortools_deps//:requirements.bzl", "requirement")

def code_sample_cc(name):
    native.cc_binary(
        name = name + "_cc",
        srcs = [name + ".cc"],
        deps = [
            "//ortools/base",
            "//ortools/linear_solver",
            "//ortools/linear_solver:linear_solver_cc_proto",
        ],
    )

    native.cc_test(
        name = name + "_cc_test",
        size = "small",
        srcs = [name + ".cc"],
        deps = [
            ":" + name + "_cc",
            "//ortools/base",
            "//ortools/linear_solver",
            "//ortools/linear_solver:linear_solver_cc_proto",
        ],
    )

def code_sample_py(name):
    native.py_binary(
        name = name + "_py3",
        srcs = [name + ".py"],
        main = name + ".py",
        deps = [
            requirement("absl-py"),
            requirement("numpy"),
            "//ortools/linear_solver/python:model_builder",
        ],
        python_version = "PY3",
        srcs_version = "PY3",
    )

    native.py_test(
        name = name + "_py_test",
        size = "small",
        srcs = [name + ".py"],
        main = name + ".py",
        data = [
            "//ortools/linear_solver/python:model_builder",
        ],
        deps = [
            requirement("absl-py"),
            requirement("numpy"),
        ],
        python_version = "PY3",
        srcs_version = "PY3",
    )

def code_sample_java(name):
    native.java_binary(
        name = name + "_java",
        srcs = [name + ".java"],
        main_class = "com.google.ortools.linearsolver.samples." + name,
        data = [
            "//ortools/java/com/google/ortools:libjniortools.so",
            "//ortools/java/com/google/ortools:libjniortools.dylib",
            "//ortools/java/com/google/ortools:jniortools.dll",
        ],
        deps = [
            "//ortools/linear_solver/java:modelbuilder",
            "//ortools/java/com/google/ortools/modelbuilder",
            "//ortools/java/com/google/ortools:Loader"
        ],
    )

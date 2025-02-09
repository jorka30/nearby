# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
licenses(["notice"])

cc_library(
    name = "presence",
    srcs = [
        "presence_client.cc",
    ],
    hdrs = [
        "presence_client.h",
    ],
    visibility = [
        "//third_party/nearby:__subpackages__",
    ],
    deps = [
        ":types",
    ],
)

cc_library(
    name = "types",
    srcs = [
        "device_motion.cc",
        "discovery_filter.cc",
        "presence_action.cc",
        "presence_client.cc",
        "presence_device.cc",
        "presence_identity.cc",
        "presence_zone.cc",
    ],
    hdrs = [
        "broadcast_options.h",
        "device_motion.h",
        "discovery_filter.h",
        "discovery_options.h",
        "listeners.h",
        "presence_action.h",
        "presence_client.h",
        "presence_device.h",
        "presence_identity.h",
        "presence_zone.h",
        "status.h",
    ],
    visibility = [
        "//third_party/nearby/presence:__subpackages__",
    ],
    deps = [
        "//internal/platform:base",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
        "@com_google_glog//:glog",
    ],
)

cc_library(
    name = "broadcast_request",
    srcs = ["broadcast_request.cc"],
    hdrs = ["broadcast_request.h"],
    deps = [
        ":encryption",
        ":types",
        "//internal/platform:logging",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "encryption",
    srcs = ["encryption.cc"],
    hdrs = ["encryption.h"],
    deps = [
        "//internal/platform:logging",
        "//third_party/tink/cc/subtle",
        "//util/random:ssl_bit_gen",
        "@boringssl//:crypto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "broadcast_request_test",
    size = "small",
    srcs = ["broadcast_request_test.cc"],
    deps = [
        ":broadcast_request",
        "//internal/platform/implementation/g3",  # build_cleaner: keep
        "@com_github_protobuf_matchers//protobuf-matchers",
        "@com_google_absl//absl/types:variant",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "encryption_test",
    size = "small",
    srcs = ["encryption_test.cc"],
    deps = [
        ":encryption",
        "//internal/platform/implementation/g3",  # build_cleaner: keep
        "@com_github_protobuf_matchers//protobuf-matchers",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "types_test",
    size = "small",
    srcs = [
        "broadcast_options_test.cc",
        "device_motion_test.cc",
        "discovery_filter_test.cc",
        "discovery_options_test.cc",
        "presence_action_test.cc",
        "presence_device_test.cc",
        "presence_identity_test.cc",
        "presence_zone_test.cc",
        "status_test.cc",
    ],
    shard_count = 6,
    deps = [
        ":types",
        "@com_github_protobuf_matchers//protobuf-matchers",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "presence_test",
    size = "small",
    srcs = [
        "presence_client_test.cc",
    ],
    shard_count = 6,
    deps = [
        ":presence",
        "@com_github_protobuf_matchers//protobuf-matchers",
        "@com_google_googletest//:gtest_main",
    ],
)

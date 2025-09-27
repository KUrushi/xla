# Load the necessary Bazel rules
load("//xla/tsl/platform:rules_cc.bzl", "cc_library")

cc_library(
    name = "xla_wrapper_lib",
    srcs = ["xla_wrapper.cpp"],
    copts = ["-g"],  # It's good to keep debug options for now
    deps = [
        # This is the single, correct dependency for the PJRT CPU C API
        "//xla/pjrt/c:pjrt_c_api_cpu",
        "//xla/pjrt:pjrt_client",

    ],
)

cc_binary(
    name = "libxla_wrapper.so",
    linkshared = True,  # This attribute correctly belongs to cc_binary
    deps = [
        ":xla_wrapper_lib",  # This links the code compiled in Step 1
        "//xla/pjrt/proto:compile_options_proto_cc",
        "//xla/service:computation_placer_hdr",
    ],
)

cc_binary(
    name = "minimal_test",
    srcs = ["minimal_test.cpp"],
    deps = [
        "//xla/pjrt/c:pjrt_c_api_cpu",
        # Dependencies needed to build the CompileOptions proto
        "//xla/pjrt/proto:compile_options_proto_cc",
        "//xla/service:computation_placer_hdr",
    ],
)

# load("//xla/tsl/platform:rules_cc.bzl", "cc_library")
# cc_library(
#     name = "xla_lisp_bridge_lib",
#     srcs = ["xla_wrapper.cpp"],
#     # PJRT C APIのヘッダとライブラリに依存することをBazelに伝える
#     deps = [
#          "//xla/pjrt/c:pjrt_c_api_cpu",
#          "//xla/pjrt:pjrt_api",
#          "//xla/pjrt/c:pjrt_c_api_hdrs",
#          "//xla/pjrt/c:pjrt_c_api_macros",
#          "//xla/pjrt/c:cpu_pjrt_c_api_impl"
#     ],
# )

# # 共有ライブラリ(.so)としてビルドするルール
# cc_binary(
#     name = "libxla_wrapper.so",
#     linkshared = True, # これが共有ライブラリを作るためのキー
#     deps = [":xla_lisp_bridge_lib"],
# )

// In file: xla/lisp_bridge/minimal_test.cpp

#include <iostream>
#include <string>
#include <cstring>
#include "xla/pjrt/c/pjrt_c_api.h"
#include "xla/pjrt/c/pjrt_c_api_cpu.h"

// We must include these to correctly build the CompileOptions
#include "xla/pjrt/pjrt_client.h"
#include "xla/client/executable_build_options.h"
#include "xla/service/computation_placer.h"


// Error handling function
void HandleError(PJRT_Error* error, const PJRT_Api* api) {
    if (error == nullptr) return;
    PJRT_Error_Message_Args msg_args;
    msg_args.struct_size = PJRT_Error_Message_Args_STRUCT_SIZE;
    msg_args.extension_start = nullptr;
    msg_args.error = error;
    api->PJRT_Error_Message(&msg_args);
    std::cerr << "PJRT call failed: " << std::string(msg_args.message, msg_args.message_size) << "\n";
    PJRT_Error_Destroy_Args des_args;
    des_args.struct_size = PJRT_Error_Destroy_Args_STRUCT_SIZE;
    des_args.extension_start = nullptr;
    des_args.error = error;
    api->PJRT_Error_Destroy(&des_args);
    exit(1);
}
#define PJRT_CHECK(expr, api) HandleError((expr), (api))

// ** HELPER FUNCTION MODELED ON XLA'S OFFICIAL TEST SUITE (Corrected) **
// Creates a serialized CompileOptions string for a single-device computation.
std::string BuildCompileOptions() {
    // 1. Use the C++ helper class, not the raw Proto class.
    xla::CompileOptions options;

    // 2. Create a device assignment for 1 replica on device 0.
    xla::DeviceAssignment device_assignment(1, 1);
    device_assignment(0, 0) = 0;

    // 3. Set the device assignment on the nested 'executable_build_options'.
    options.executable_build_options.set_device_assignment(device_assignment);

    // 4. Convert the whole structure to a Proto and serialize it.
    return options.ToProto()->SerializeAsString();
}

int main() {
    std::cout << "--- 1. Initializing PJRT API... ---" << std::endl;
    const PJRT_Api* api = GetPjrtApi();

    PJRT_Plugin_Initialize_Args init_args;
    init_args.struct_size = PJRT_Plugin_Initialize_Args_STRUCT_SIZE;
    init_args.extension_start = nullptr;
    PJRT_CHECK(api->PJRT_Plugin_Initialize(&init_args), api);
    std::cout << "--- API Initialized Successfully ---" << std::endl;

    std::cout << "\n--- 2. Creating PJRT Client... ---" << std::endl;
    PJRT_Client_Create_Args create_args;
    create_args.struct_size = PJRT_Client_Create_Args_STRUCT_SIZE;
    create_args.extension_start = nullptr;
    create_args.client = nullptr;
    create_args.create_options = nullptr;
    create_args.num_options = 0;
    create_args.kv_get_callback = nullptr;
    create_args.kv_put_callback = nullptr;
    create_args.kv_try_get_callback = nullptr;
    PJRT_CHECK(api->PJRT_Client_Create(&create_args), api);
    PJRT_Client* client = create_args.client;
    std::cout << "--- Client Created Successfully ---" << std::endl;

    std::cout << "\n--- 3. Compiling Program... ---" << std::endl;
    const char* hlo_string =
        "module @jit_add_vectors { func.func public @main(%arg0: tensor<2xf32>, %arg1: tensor<2xf32>) -> (tensor<2xf32>) {\n"
        "  %0 = stablehlo.add %arg0, %arg1 : tensor<2xf32>\n"
        "  return %0 : tensor<2xf32>\n"
        "}}";

    PJRT_Program program;
    program.struct_size = PJRT_Program_STRUCT_SIZE;
    program.extension_start = nullptr;
    program.code = const_cast<char*>(hlo_string);
    program.code_size = strlen(hlo_string);
    program.format = "mlir";
    program.format_size = strlen("mlir");

    std::string options_str = BuildCompileOptions();

    PJRT_Client_Compile_Args compile_args;
    compile_args.struct_size = PJRT_Client_Compile_Args_STRUCT_SIZE;
    compile_args.extension_start = nullptr;
    compile_args.client = client;
    compile_args.program = &program;
    compile_args.compile_options = options_str.c_str();
    compile_args.compile_options_size = options_str.length();

    PJRT_CHECK(api->PJRT_Client_Compile(&compile_args), api);

    std::cout << "--- Compilation Successful ---" << std::endl;

    // Cleanup
    PJRT_LoadedExecutable_Destroy_Args destroy_exe_args;
    destroy_exe_args.struct_size = PJRT_LoadedExecutable_Destroy_Args_STRUCT_SIZE;
    destroy_exe_args.extension_start = nullptr;
    destroy_exe_args.executable = compile_args.executable;
    api->PJRT_LoadedExecutable_Destroy(&destroy_exe_args);

    PJRT_Client_Destroy_Args destroy_client_args;
    destroy_client_args.struct_size = PJRT_Client_Destroy_Args_STRUCT_SIZE;
    destroy_client_args.extension_start = nullptr;
    destroy_client_args.client = client;
    api->PJRT_Client_Destroy(&destroy_client_args);

    std::cout << "\n--- Test Succeeded ---" << std::endl;
    return 0;
}

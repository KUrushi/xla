#include "xla/pjrt/c/pjrt_c_api.h"
#include "xla/pjrt/c/pjrt_c_api_cpu.h"

// We must include these to correctly build the CompileOptions
#include "xla/pjrt/pjrt_client.h"
#include "xla/client/executable_build_options.h"
#include "xla/service/computation_placer.h"

#include <chrono>   // For timestamps
#include <cstdarg>  // For variadic logging function
#include <cstring>  // For strlen
#include <exception>
#include <fstream>  // For logging to a file
#include <iomanip>  // For formatting timestamps
#include <iostream>
#include <mutex>  // For thread-safe logging
#include <stdexcept>
#include <string>
#include <vector>

// --- Simple Logging Utility ---
std::ofstream g_log_file;
std::mutex g_log_mutex;

// Variadic logging function to allow printf-style formatting
void LogDebug(const char* format, ...) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (!g_log_file.is_open()) return;

  // Get current time for the timestamp
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);

  // Format the user's message
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // Write timestamp and message to the log file
  g_log_file << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X")
             << " | " << buffer << std::endl;
}

// Macro to make logging easier to call
#define LOG_DEBUG(...) LogDebug(__VA_ARGS__)

// --- Global API pointer ---
const PJRT_Api* g_pjrt_api = nullptr;

// --- Safe API Accessor ---
const PJRT_Api* GetApi() {
  if (g_pjrt_api == nullptr) {
    throw std::runtime_error(
        "PJRT API not initialized. Call initialize_pjrt_api first.");
  }
  return g_pjrt_api;
}

// --- Robust Error Handling ---
void CheckError(PJRT_Error* error, const PJRT_Api* api) {
  if (error == nullptr) {
    return;
  }
  if (api == nullptr) {
    LOG_DEBUG("PJRT Error occurred, but the PJRT_Api pointer was null.");
    std::cerr << "PJRT Error occurred, but the PJRT_Api pointer was null. "
                 "Cannot process error."
              << std::endl;
    return;
  }
  std::string error_message = "No message available.";
  if (api->PJRT_Error_Message) {
    PJRT_Error_Message_Args message_args;
    message_args.struct_size = PJRT_Error_Message_Args_STRUCT_SIZE;
    message_args.extension_start = nullptr;
    message_args.error = error;
    api->PJRT_Error_Message(&message_args);
    error_message =
        std::string(message_args.message, message_args.message_size);
  }
  if (api->PJRT_Error_Destroy) {
    PJRT_Error_Destroy_Args destroy_args;
    destroy_args.struct_size = PJRT_Error_Destroy_Args_STRUCT_SIZE;
    destroy_args.extension_start = nullptr;
    destroy_args.error = error;
    api->PJRT_Error_Destroy(&destroy_args);
  }
  LOG_DEBUG("PJRT Error Encountered: %s", error_message.c_str());
  throw std::runtime_error("PJRT Error: " + error_message);
}

#define CHECK_ERROR(error, api) CheckError(error, api)

// --- FFI Functions ---
extern "C" {

void initialize_pjrt_api() {
  // Open the log file, overwriting any previous content
  g_log_file.open("/tmp/xla_debug.log", std::ios::out | std::ios::trunc);
  LOG_DEBUG("Entering initialize_pjrt_api");
  if (g_pjrt_api != nullptr) {
    LOG_DEBUG("Warning: PJRT API already initialized.");
    return;
  }
  try {
    LOG_DEBUG("Calling GetPjrtApi()");
    g_pjrt_api = GetPjrtApi();
    if (g_pjrt_api == nullptr) {
      throw std::runtime_error(
          "GetPjrtApi() returned nullptr. Check PJRT installation.");
    }
    LOG_DEBUG("GetPjrtApi() successful.");

    PJRT_Plugin_Initialize_Args init_args;
    init_args.struct_size = PJRT_Plugin_Initialize_Args_STRUCT_SIZE;
    init_args.extension_start = nullptr;
    LOG_DEBUG("Calling PJRT_Plugin_Initialize");
    CHECK_ERROR(g_pjrt_api->PJRT_Plugin_Initialize(&init_args), g_pjrt_api);
    LOG_DEBUG("PJRT_Plugin_Initialize successful.");
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception during API initialization: %s", e.what());
    std::cerr << "Exception during PJRT API initialization: " << e.what()
              << std::endl;
    g_pjrt_api = nullptr;  // Ensure it's null on failure
  }
  LOG_DEBUG("Exiting initialize_pjrt_api");
}

void shutdown_pjrt_api() {
  LOG_DEBUG("Entering shutdown_pjrt_api");
  if (g_pjrt_api != nullptr) {
    g_pjrt_api = nullptr;
    LOG_DEBUG("PJRT API shut down.");
    std::cout << "PJRT API shut down." << std::endl;
  }
  if (g_log_file.is_open()) {
    LOG_DEBUG("Closing log file.");
    g_log_file.close();
  }
}

PJRT_Client* create_client() {
  LOG_DEBUG("Entering create_client");
  try {
    const PJRT_Api* api = GetApi();  // Will throw if not initialized

    PJRT_Client_Create_Args create_args;
    create_args.struct_size = PJRT_Client_Create_Args_STRUCT_SIZE;
    create_args.extension_start = nullptr;
    create_args.client = nullptr;
    create_args.create_options = nullptr;
    create_args.num_options = 0;
    create_args.kv_get_callback = nullptr;
    create_args.kv_put_callback = nullptr;
    create_args.kv_try_get_callback = nullptr;

    LOG_DEBUG("Calling PJRT_Client_Create");
    CHECK_ERROR(api->PJRT_Client_Create(&create_args), api);
    LOG_DEBUG("PJRT_Client_Create successful. Client ptr: %p",
              create_args.client);
    return create_args.client;

  } catch (const std::exception& e) {
    LOG_DEBUG("Exception during client creation: %s", e.what());
    std::cerr << "Exception during client creation: " << e.what() << std::endl;
    return nullptr;
  }
}

void destroy_client(PJRT_Client* client) {
  LOG_DEBUG("Entering destroy_client for client: %p", client);
  if (client == nullptr) return;
  try {
    const PJRT_Api* api = GetApi();
    PJRT_Client_Destroy_Args destroy_args;
    destroy_args.struct_size = PJRT_Client_Destroy_Args_STRUCT_SIZE;
    destroy_args.extension_start = nullptr;
    destroy_args.client = client;
    LOG_DEBUG("Calling PJRT_Client_Destroy");
    CHECK_ERROR(api->PJRT_Client_Destroy(&destroy_args), api);
    LOG_DEBUG("PJRT_Client_Destroy successful.");
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception during client destruction: %s", e.what());
    std::cerr << "Exception during client destruction: " << e.what()
              << std::endl;
  }
}

PJRT_Device* get_device(PJRT_Client* client, size_t device_index) {
  LOG_DEBUG("Entering get_device for client: %p, index: %zu", client,
            device_index);
  try {
    const PJRT_Api* api = GetApi();
    PJRT_Client_LookupDevice_Args args;
    args.struct_size = PJRT_Client_LookupDevice_Args_STRUCT_SIZE;
    args.extension_start = nullptr;
    args.client = client;
    args.id = static_cast<int>(device_index);
    LOG_DEBUG("Calling PJRT_Client_LookupDevice");
    CHECK_ERROR(api->PJRT_Client_LookupDevice(&args), api);
    LOG_DEBUG("PJRT_Client_LookupDevice successful. Device: %p", args.device);
    return args.device;
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in get_device: %s", e.what());
    std::cerr << "Exception in get_device: " << e.what() << std::endl;
    return nullptr;
  }
}

PJRT_Buffer* create_buffer_from_host(PJRT_Client* client, PJRT_Device* device,
                                     const void* data_ptr,
                                     const int64_t* dims_ptr, size_t num_dims,
                                     PJRT_Buffer_Type type) {
  LOG_DEBUG("Entering create_buffer_from_host");
  try {
    const PJRT_Api* api = GetApi();
    PJRT_Client_BufferFromHostBuffer_Args args;
    args.struct_size = PJRT_Client_BufferFromHostBuffer_Args_STRUCT_SIZE;
    args.extension_start = nullptr;
    args.client = client;
    args.data = data_ptr;
    args.type = type;
    args.dims = dims_ptr;
    args.num_dims = num_dims;
    args.byte_strides = nullptr;
    args.num_byte_strides = 0;
    args.host_buffer_semantics =
        PJRT_HostBufferSemantics_kImmutableUntilTransferCompletes;
    args.device = device;
    args.memory = nullptr;
    args.device_layout = nullptr;

    LOG_DEBUG("Calling PJRT_Client_BufferFromHostBuffer");
    CHECK_ERROR(api->PJRT_Client_BufferFromHostBuffer(&args), api);
    LOG_DEBUG("PJRT_Client_BufferFromHostBuffer successful. Event: %p",
              args.done_with_host_buffer);

    PJRT_Event* event = args.done_with_host_buffer;
    PJRT_Event_Await_Args await_args;
    await_args.struct_size = PJRT_Event_Await_Args_STRUCT_SIZE;
    await_args.extension_start = nullptr;
    await_args.event = event;
    LOG_DEBUG("Calling PJRT_Event_Await");
    CHECK_ERROR(api->PJRT_Event_Await(&await_args), api);
    LOG_DEBUG("PJRT_Event_Await successful.");

    PJRT_Event_Destroy_Args destroy_args;
    destroy_args.struct_size = PJRT_Event_Destroy_Args_STRUCT_SIZE;
    destroy_args.extension_start = nullptr;
    destroy_args.event = event;
    LOG_DEBUG("Calling PJRT_Event_Destroy");
    CHECK_ERROR(api->PJRT_Event_Destroy(&destroy_args), api);
    LOG_DEBUG("PJRT_Event_Destroy successful.");

    LOG_DEBUG("Exiting create_buffer_from_host, returning buffer: %p",
              args.buffer);
    return args.buffer;
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in create_buffer_from_host: %s", e.what());
    std::cerr << "Exception in create_buffer_from_host: " << e.what()
              << std::endl;
    return nullptr;
  }
}

// ** MODIFIED FUNCTION **
PJRT_LoadedExecutable* compile_add_program(PJRT_Client* client) {
  LOG_DEBUG("Entering compile_add_program for client: %p", client);
  try {
    const PJRT_Api* api = GetApi();
    // This MLIR string now defines a function that adds two 2x3 matrices.
    const char* hlo_string =
        "module @jit_add_matrices attributes {mhlo.num_partitions = 1 : i32, mhlo.num_replicas = 1 : i32} {\n"
        "  func.func public @main(%arg0: tensor<2x3xf32>, %arg1: tensor<2x3xf32>) -> (tensor<2x3xf32>) {\n"
        "    %0 = stablehlo.add %arg0, %arg1 : tensor<2x3xf32>\n"
        "    return %0 : tensor<2x3xf32>\n"
        "  }\n"
        "}";

    PJRT_Program program;
    program.struct_size = PJRT_Program_STRUCT_SIZE;
    program.extension_start = nullptr;
    program.code = const_cast<char*>(hlo_string);
    program.code_size = strlen(hlo_string);
    program.format = "mlir";
    program.format_size = strlen("mlir");

    xla::CompileOptions options;
    xla::DeviceAssignment device_assignment(1, 1);
    device_assignment(0, 0) = 0;
    options.executable_build_options.set_device_assignment(device_assignment);
    std::string options_str = options.ToProto()->SerializeAsString();

    PJRT_Client_Compile_Args compile_args;
    compile_args.struct_size = PJRT_Client_Compile_Args_STRUCT_SIZE;
    compile_args.extension_start = nullptr;
    compile_args.client = client;
    compile_args.program = &program;
    compile_args.compile_options = options_str.c_str();
    compile_args.compile_options_size = options_str.length();

    LOG_DEBUG("Calling PJRT_Client_Compile");
    CHECK_ERROR(api->PJRT_Client_Compile(&compile_args), api);
    LOG_DEBUG("PJRT_Client_Compile successful. Returning Executable: %p",
              compile_args.executable);

    return compile_args.executable;
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in compile_add_program: %s", e.what());
    std::cerr << "Exception in compile_add_program: " << e.what() << std::endl;
    return nullptr;
  }
}

PJRT_Buffer* execute_add(PJRT_LoadedExecutable* executable,
                         PJRT_Buffer* buffer_a, PJRT_Buffer* buffer_b) {
  LOG_DEBUG(
      "Entering execute_add with executable: %p, buffer_a: %p, buffer_b: %p",
      executable, buffer_a, buffer_b);
  try {
    const PJRT_Api* api = GetApi();

    PJRT_LoadedExecutable_Execute_Args execute_args = {};
    execute_args.struct_size = PJRT_LoadedExecutable_Execute_Args_STRUCT_SIZE;
    execute_args.executable = executable;

    PJRT_ExecuteOptions options = {};
    options.struct_size = PJRT_ExecuteOptions_STRUCT_SIZE;
    execute_args.options = &options;

    PJRT_Buffer* inputs[] = {buffer_a, buffer_b};
    PJRT_Buffer** inputs_per_device[] = {inputs};
    execute_args.argument_lists = inputs_per_device;
    execute_args.num_devices = 1;
    execute_args.num_args = 2;

    PJRT_Buffer* output_buffer = nullptr;
    PJRT_Buffer** output_list[] = {&output_buffer};
    execute_args.output_lists = output_list;

    PJRT_Event* event_list[1] = {nullptr};
    execute_args.device_complete_events = event_list;

    LOG_DEBUG("Calling PJRT_LoadedExecutable_Execute");
    CHECK_ERROR(api->PJRT_LoadedExecutable_Execute(&execute_args), api);

    PJRT_Event* device_complete_event = event_list[0];
    LOG_DEBUG("PJRT_LoadedExecutable_Execute successful. Event: %p",
              device_complete_event);

    PJRT_Event_Await_Args await_args = {};
    await_args.struct_size = PJRT_Event_Await_Args_STRUCT_SIZE;
    await_args.event = device_complete_event;
    LOG_DEBUG("Calling PJRT_Event_Await on event %p", device_complete_event);
    CHECK_ERROR(api->PJRT_Event_Await(&await_args), api);
    LOG_DEBUG("PJRT_Event_Await successful.");

    PJRT_Event_Destroy_Args destroy_event_args = {};
    destroy_event_args.struct_size = PJRT_Event_Destroy_Args_STRUCT_SIZE;
    destroy_event_args.event = device_complete_event;
    LOG_DEBUG("Calling PJRT_Event_Destroy");
    CHECK_ERROR(api->PJRT_Event_Destroy(&destroy_event_args), api);
    LOG_DEBUG("PJRT_Event_Destroy successful.");

    LOG_DEBUG("Exiting execute_add, returning output buffer: %p",
              output_buffer);
    return output_buffer;
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in execute_add: %s", e.what());
    std::cerr << "Exception in execute_add: " << e.what() << std::endl;
    return nullptr;
  }
}

void buffer_to_host(PJRT_Buffer* buffer, void* data_ptr, size_t byte_size) {
  LOG_DEBUG("Entering buffer_to_host for buffer: %p", buffer);
  try {
    const PJRT_Api* api = GetApi();
    PJRT_Buffer_ToHostBuffer_Args args;
    args.struct_size = PJRT_Buffer_ToHostBuffer_Args_STRUCT_SIZE;
    args.extension_start = nullptr;
    args.src = buffer;
    args.dst = data_ptr;
    args.dst_size = byte_size;
    args.host_layout = nullptr;

    LOG_DEBUG("Calling PJRT_Buffer_ToHostBuffer");
    CHECK_ERROR(api->PJRT_Buffer_ToHostBuffer(&args), api);
    LOG_DEBUG("PJRT_Buffer_ToHostBuffer successful. Event: %p", args.event);

    PJRT_Event* event = args.event;
    PJRT_Event_Await_Args await_args;
    await_args.struct_size = PJRT_Event_Await_Args_STRUCT_SIZE;
    await_args.extension_start = nullptr;
    await_args.event = event;
    LOG_DEBUG("Calling PJRT_Event_Await");
    CHECK_ERROR(api->PJRT_Event_Await(&await_args), api);
    LOG_DEBUG("PJRT_Event_Await successful.");

    PJRT_Event_Destroy_Args destroy_args;
    destroy_args.struct_size = PJRT_Event_Destroy_Args_STRUCT_SIZE;
    destroy_args.extension_start = nullptr;
    destroy_args.event = event;
    LOG_DEBUG("Calling PJRT_Event_Destroy");
    CHECK_ERROR(api->PJRT_Event_Destroy(&destroy_args), api);
    LOG_DEBUG("PJRT_Event_Destroy successful.");
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in buffer_to_host: %s", e.what());
    std::cerr << "Exception in buffer_to_host: " << e.what() << std::endl;
  }
  LOG_DEBUG("Exiting buffer_to_host");
}

// --- Generic compile: accepts arbitrary MLIR string from Lisp ---
PJRT_LoadedExecutable* compile_program(PJRT_Client* client,
                                       const char* mlir_string,
                                       size_t mlir_length) {
  LOG_DEBUG("Entering compile_program for client: %p, mlir_length: %zu",
            client, mlir_length);
  try {
    const PJRT_Api* api = GetApi();

    PJRT_Program program;
    program.struct_size = PJRT_Program_STRUCT_SIZE;
    program.extension_start = nullptr;
    program.code = const_cast<char*>(mlir_string);
    program.code_size = mlir_length;
    program.format = "mlir";
    program.format_size = strlen("mlir");

    xla::CompileOptions options;
    xla::DeviceAssignment device_assignment(1, 1);
    device_assignment(0, 0) = 0;
    options.executable_build_options.set_device_assignment(device_assignment);
    std::string options_str = options.ToProto()->SerializeAsString();

    PJRT_Client_Compile_Args compile_args;
    compile_args.struct_size = PJRT_Client_Compile_Args_STRUCT_SIZE;
    compile_args.extension_start = nullptr;
    compile_args.client = client;
    compile_args.program = &program;
    compile_args.compile_options = options_str.c_str();
    compile_args.compile_options_size = options_str.length();

    LOG_DEBUG("Calling PJRT_Client_Compile");
    CHECK_ERROR(api->PJRT_Client_Compile(&compile_args), api);
    LOG_DEBUG("PJRT_Client_Compile successful. Returning Executable: %p",
              compile_args.executable);

    return compile_args.executable;
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in compile_program: %s", e.what());
    std::cerr << "Exception in compile_program: " << e.what() << std::endl;
    return nullptr;
  }
}

// --- Generic execute: variable number of input buffers, single output ---
PJRT_Buffer* execute_program(PJRT_LoadedExecutable* executable,
                             PJRT_Buffer** input_buffers,
                             size_t num_inputs) {
  LOG_DEBUG("Entering execute_program with executable: %p, num_inputs: %zu",
            executable, num_inputs);
  try {
    const PJRT_Api* api = GetApi();

    PJRT_LoadedExecutable_Execute_Args execute_args = {};
    execute_args.struct_size = PJRT_LoadedExecutable_Execute_Args_STRUCT_SIZE;
    execute_args.executable = executable;

    PJRT_ExecuteOptions options = {};
    options.struct_size = PJRT_ExecuteOptions_STRUCT_SIZE;
    execute_args.options = &options;

    PJRT_Buffer** inputs_per_device[] = {input_buffers};
    execute_args.argument_lists = inputs_per_device;
    execute_args.num_devices = 1;
    execute_args.num_args = num_inputs;

    PJRT_Buffer* output_buffer = nullptr;
    PJRT_Buffer** output_list[] = {&output_buffer};
    execute_args.output_lists = output_list;

    PJRT_Event* event_list[1] = {nullptr};
    execute_args.device_complete_events = event_list;

    LOG_DEBUG("Calling PJRT_LoadedExecutable_Execute");
    CHECK_ERROR(api->PJRT_LoadedExecutable_Execute(&execute_args), api);

    PJRT_Event* device_complete_event = event_list[0];
    LOG_DEBUG("PJRT_LoadedExecutable_Execute successful. Event: %p",
              device_complete_event);

    PJRT_Event_Await_Args await_args = {};
    await_args.struct_size = PJRT_Event_Await_Args_STRUCT_SIZE;
    await_args.event = device_complete_event;
    LOG_DEBUG("Calling PJRT_Event_Await on event %p", device_complete_event);
    CHECK_ERROR(api->PJRT_Event_Await(&await_args), api);
    LOG_DEBUG("PJRT_Event_Await successful.");

    PJRT_Event_Destroy_Args destroy_event_args = {};
    destroy_event_args.struct_size = PJRT_Event_Destroy_Args_STRUCT_SIZE;
    destroy_event_args.event = device_complete_event;
    LOG_DEBUG("Calling PJRT_Event_Destroy");
    CHECK_ERROR(api->PJRT_Event_Destroy(&destroy_event_args), api);
    LOG_DEBUG("PJRT_Event_Destroy successful.");

    LOG_DEBUG("Exiting execute_program, returning output buffer: %p",
              output_buffer);
    return output_buffer;
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in execute_program: %s", e.what());
    std::cerr << "Exception in execute_program: " << e.what() << std::endl;
    return nullptr;
  }
}

// --- Buffer cleanup ---
void destroy_buffer(PJRT_Buffer* buffer) {
  LOG_DEBUG("Entering destroy_buffer for buffer: %p", buffer);
  if (buffer == nullptr) return;
  try {
    const PJRT_Api* api = GetApi();
    PJRT_Buffer_Destroy_Args args;
    args.struct_size = PJRT_Buffer_Destroy_Args_STRUCT_SIZE;
    args.extension_start = nullptr;
    args.buffer = buffer;
    LOG_DEBUG("Calling PJRT_Buffer_Destroy");
    CHECK_ERROR(api->PJRT_Buffer_Destroy(&args), api);
    LOG_DEBUG("PJRT_Buffer_Destroy successful.");
  } catch (const std::exception& e) {
    LOG_DEBUG("Exception in destroy_buffer: %s", e.what());
    std::cerr << "Exception in destroy_buffer: " << e.what() << std::endl;
  }
}

void destroy_executable(PJRT_LoadedExecutable* executable) {
    LOG_DEBUG("Entering destroy_executable for executable: %p", executable);
    if (executable == nullptr) return;
    try {
        const PJRT_Api* api = GetApi();
        PJRT_LoadedExecutable_Destroy_Args args;
        args.struct_size = PJRT_LoadedExecutable_Destroy_Args_STRUCT_SIZE;
        args.extension_start = nullptr;
        args.executable = executable;
        LOG_DEBUG("Calling PJRT_LoadedExecutable_Destroy");
        CHECK_ERROR(api->PJRT_LoadedExecutable_Destroy(&args), api);
        LOG_DEBUG("PJRT_LoadedExecutable_Destroy successful.");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception during executable destruction: %s", e.what());
        std::cerr << "Exception during executable destruction: " << e.what()
                  << std::endl;
    }
}
}

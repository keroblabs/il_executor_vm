#ifndef IL_EXECUTOR_EXECUTOR_H
#define IL_EXECUTOR_EXECUTOR_H

#include "stack.h"
#include "value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EXECUTOR_IDLE = 0,
    EXECUTOR_CONTINUE,
    EXECUTOR_RETURNED,
    EXECUTOR_INVALID_ARGUMENT,
    EXECUTOR_UNSUPPORTED_OPCODE,
    EXECUTOR_TRUNCATED_INSTRUCTION,
    EXECUTOR_END_OF_CODE,
    EXECUTOR_STACK_UNDERFLOW,
    EXECUTOR_STACK_OVERFLOW,
    EXECUTOR_TYPE_MISMATCH,
    EXECUTOR_BAD_RETURN,
    EXECUTOR_INVALID_STATE,
    EXECUTOR_BAD_INDEX,
    EXECUTOR_BAD_BRANCH_TARGET,
    EXECUTOR_BAD_METHOD
} executor_status_t;

typedef enum {
    EXECUTOR_TYPE_INT8 = VALUE_T_INT8,
    EXECUTOR_TYPE_UINT8 = VALUE_T_UINT8,
    EXECUTOR_TYPE_INT16 = VALUE_T_INT16,
    EXECUTOR_TYPE_UINT16 = VALUE_T_UINT16,
    EXECUTOR_TYPE_INT32 = VALUE_T_INT32,
    EXECUTOR_TYPE_UINT32 = VALUE_T_UINT32,
    EXECUTOR_TYPE_INT64 = VALUE_T_INT64,
    EXECUTOR_TYPE_UINT64 = VALUE_T_UINT64,
    EXECUTOR_TYPE_FLOAT32 = VALUE_T_FLOAT32,
    EXECUTOR_TYPE_FLOAT64 = VALUE_T_FLOAT64,
    EXECUTOR_TYPE_NATIVE_INT = VALUE_T_NATIVE_INT,
    EXECUTOR_TYPE_NATIVE_UINT = VALUE_T_NATIVE_UINT,
    EXECUTOR_TYPE_REF = VALUE_T_COUNT,
    EXECUTOR_TYPE_BYREF,
    EXECUTOR_TYPE_COUNT
} executor_type_t;

typedef struct {
    const uint8_t * code;
    uint32_t code_size;
    uint16_t method_id;
    uint16_t max_stack;
    uint8_t return_kind;
    uint16_t argument_count;
    uint16_t local_count;
    const uint8_t * argument_types;
    const uint8_t * local_types;
} executor_method_t;

typedef struct {
    const executor_method_t * methods;
    uint32_t method_count;
} executor_program_t;

typedef struct {
    stack_thread_t * thread;
    stack_frame_t * frame;
    executor_method_t method;
    uint32_t offset;
    executor_status_t status;
    const executor_program_t * program;
} executor_t;

executor_status_t executor_start(executor_t * executor, stack_thread_t * thread,
                                 const executor_method_t * method);
executor_status_t executor_start_program(executor_t * executor, stack_thread_t * thread,
                                         const executor_program_t * program, uint16_t entry_id);
executor_status_t executor_step(executor_t * executor);
executor_status_t executor_run(executor_t * executor, uint32_t instruction_budget);

#ifdef __cplusplus
}
#endif

#endif

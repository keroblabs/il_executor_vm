#include <math.h>
#include <string.h>
#include "executor.h"
#include "fake_embedder.h"
#include "il_opcodes.h"

#define PAYLOAD 4096u
#define HANDLES 128u
#define SLOTS 8u
#define FRAMES 2u
#define GUARD 0x5A

static stack_slot_t slots[SLOTS + 1];
static stack_frame_t frames[FRAMES + 1];
static stack_thread_t thread;
static executor_t executor;

static void start_thread(uint32_t frame_count)
{
    if (thread.stack_base != NULL)
        stack_thread_end(&thread);
    if (!obj_handler_is_ready(&fake_oh))
        fake_init(PAYLOAD, HANDLES);
    memset(&executor, 0, sizeof executor);
    memset(&thread, 0, sizeof thread);
    memset(slots, GUARD, sizeof slots);
    memset(frames, GUARD, sizeof frames);
    CHECK(stack_thread_init(&fake_oh, &thread, slots, SLOTS, frames, frame_count));
}

static executor_status_t start_method(const uint8_t * code, uint32_t code_size,
                                      uint16_t max_stack, uint8_t return_kind)
{
    executor_method_t method;

    memset(&method, 0, sizeof method);
    method.code = code;
    method.code_size = code_size;
    method.method_id = 1;
    method.max_stack = max_stack;
    method.return_kind = return_kind;
    return executor_start(&executor, &thread, &method);
}

static void check_stack(void)
{
    const uint8_t * slot_guard = (const uint8_t *)&slots[SLOTS];
    const uint8_t * frame_guard = (const uint8_t *)&frames[FRAMES];
    size_t index;

    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
    for (index = 0; index < sizeof slots[SLOTS]; index++)
        CHECK_EQ(slot_guard[index], GUARD);
    for (index = 0; index < sizeof frames[FRAMES]; index++)
        CHECK_EQ(frame_guard[index], GUARD);
}

TEST_CASE(returns_five_using_object_handler_frames)
{
    static const uint8_t code[] = { IL_OP_LDC_I4_2, IL_OP_LDC_I4_3, IL_OP_ADD, IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK(thread.frame == frames);
    CHECK_EQ(thread.frame->method, 1);
    CHECK_EQ(executor_run(&executor, 4), EXECUTOR_RETURNED);
    CHECK_EQ(executor.offset, sizeof code);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(stack_depth(&thread), 1);
    CHECK_EQ(stack_peek_i4(&thread, 0), 5);
    CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
    CHECK_EQ(executor_run(&executor, 10), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 5);
    CHECK_EQ(stack_depth(&thread), 0);
    check_stack();
}

TEST_CASE(void_return_pops_the_frame)
{
    static const uint8_t code[] = { IL_OP_NOP, IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(stack_depth(&thread), 0);
    check_stack();
}

TEST_CASE(single_step_and_budget_can_resume)
{
    static const uint8_t code[] = { IL_OP_NOP, IL_OP_LDC_I4_2, IL_OP_NOP,
                                  IL_OP_LDC_I4_3, IL_OP_ADD, IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 0), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 2);
    CHECK(thread.ip == code + 2);
    CHECK_EQ(stack_peek_i4(&thread, 0), 2);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    CHECK_EQ(stack_depth(&thread), 2);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 5);
    CHECK(thread.ip == code + 5);
    CHECK_EQ(stack_peek_i4(&thread, 0), 5);
    CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 5);
    check_stack();
}

static int32_t run_i4(const uint8_t * code, uint32_t code_size, uint16_t max_stack)
{
    int32_t result;

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, code_size, max_stack, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, code_size), EXECUTOR_RETURNED);
    result = stack_pop_i4(&thread);
    check_stack();
    return result;
}

static void write_i4(uint8_t * code, int32_t value)
{
    uint32_t bits = (uint32_t)value;

    code[0] = IL_OP_LDC_I4;
    code[1] = (uint8_t)bits;
    code[2] = (uint8_t)(bits >> 8);
    code[3] = (uint8_t)(bits >> 16);
    code[4] = (uint8_t)(bits >> 24);
}

static void check_fault(executor_status_t status, uint32_t offset, uint32_t depth)
{
    CHECK_EQ(executor.status, status);
    CHECK_EQ(executor.offset, offset);
    CHECK(thread.ip == executor.method.code + offset);
    CHECK(thread.frame == executor.frame);
    CHECK_EQ(stack_depth(&thread), depth);
    CHECK_EQ(executor_step(&executor), status);
    CHECK_EQ(executor_run(&executor, 10), status);
    CHECK_EQ(executor.offset, offset);
    CHECK_EQ(stack_depth(&thread), depth);
    check_stack();
}

TEST_CASE(compact_constants_cover_minus_one_through_eight)
{
    int32_t value;

    for (value = -1; value <= 8; value++) {
        uint8_t code[] = { (uint8_t)(IL_OP_LDC_I4_0 + value), IL_OP_RET };

        CHECK_EQ(run_i4(code, sizeof code, 1), value);
    }
}

TEST_CASE(short_constants_are_sign_extended)
{
    static const struct {
        uint8_t operand;
        int32_t value;
    } literals[] = {
        { 0x80, -128 }, { 0xFF, -1 }, { 0x00, 0 }, { 0x7F, 127 }
    };
    size_t index;

    for (index = 0; index < sizeof literals / sizeof literals[0]; index++) {
        uint8_t code[] = { IL_OP_LDC_I4_S, literals[index].operand, IL_OP_RET };

        CHECK_EQ(run_i4(code, sizeof code, 1), literals[index].value);
    }
}

TEST_CASE(full_constants_are_little_endian_signed_i4)
{
    static const struct {
        uint8_t bytes[4];
        int32_t value;
    } literals[] = {
        { { 0x78, 0x56, 0x34, 0x12 }, 0x12345678 },
        { { 0xFF, 0xFF, 0xFF, 0x7F }, INT32_MAX },
        { { 0x00, 0x00, 0x00, 0x80 }, INT32_MIN },
        { { 0xFF, 0xFF, 0xFF, 0xFF }, -1 }
    };
    size_t index;

    for (index = 0; index < sizeof literals / sizeof literals[0]; index++) {
        uint8_t code[] = { IL_OP_LDC_I4, 0, 0, 0, 0, IL_OP_RET };

        memcpy(code + 1, literals[index].bytes, sizeof literals[index].bytes);
        CHECK_EQ(run_i4(code, sizeof code, 1), literals[index].value);
    }
}

TEST_CASE(dup_and_pop_preserve_the_whole_slot)
{
    static const uint8_t code[] = { IL_OP_DUP, IL_OP_POP, IL_OP_RET };
    stack_slot_t before;

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
    stack_push_i4(&thread, -42);
    before = *stack_peek(&thread, 0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(stack_depth(&thread), 2);
    CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    CHECK(memcmp(stack_peek(&thread, 1), &before, sizeof before) == 0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(stack_depth(&thread), 1);
    CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), -42);
    check_stack();
}

TEST_CASE(arithmetic_preserves_operand_order_and_wraps)
{
    static const struct {
        uint8_t opcode;
        int32_t left;
        int32_t right;
        int32_t result;
    } arithmetic[] = {
        { IL_OP_ADD, 2, 3, 5 },
        { IL_OP_SUB, 2, 3, -1 },
        { IL_OP_SUB, 3, 2, 1 },
        { IL_OP_MUL, 7, -3, -21 },
        { IL_OP_ADD, INT32_MAX, 1, INT32_MIN },
        { IL_OP_ADD, INT32_MIN, -1, INT32_MAX },
        { IL_OP_SUB, INT32_MIN, 1, INT32_MAX },
        { IL_OP_SUB, INT32_MAX, -1, INT32_MIN },
        { IL_OP_MUL, 65536, 65536, 0 },
        { IL_OP_MUL, INT32_MIN, -1, INT32_MIN }
    };
    size_t index;

    for (index = 0; index < sizeof arithmetic / sizeof arithmetic[0]; index++) {
        uint8_t code[12];

        write_i4(code, arithmetic[index].left);
        write_i4(code + 5, arithmetic[index].right);
        code[10] = arithmetic[index].opcode;
        code[11] = IL_OP_RET;
        CHECK_EQ(run_i4(code, sizeof code, 2), arithmetic[index].result);
    }
}

TEST_CASE(negation_wraps_the_minimum_i4)
{
    static const int32_t operands[] = { 0, 7, -7, INT32_MIN };
    static const int32_t results[] = { 0, -7, 7, INT32_MIN };
    size_t index;

    for (index = 0; index < sizeof operands / sizeof operands[0]; index++) {
        uint8_t code[7];

        write_i4(code, operands[index]);
        code[5] = IL_OP_NEG;
        code[6] = IL_OP_RET;
        CHECK_EQ(run_i4(code, sizeof code, 1), results[index]);
    }
}

TEST_CASE(truncated_literals_fail_before_reading_operands)
{
    static const uint8_t full[] = { IL_OP_LDC_I4, 0x78, 0x56, 0x34, 0x12 };
    static const uint8_t short_code[] = { IL_OP_LDC_I4_S };
    uint32_t length;

    for (length = 1; length < sizeof full; length++) {
        start_thread(FRAMES);
        CHECK_EQ(start_method(full, length, 1, STACK_K_I4), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
        check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 0);
    }
    start_thread(FRAMES);
    CHECK_EQ(start_method(short_code, sizeof short_code, 1, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
    check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 0);
}

TEST_CASE(truncation_preserves_prior_stack_and_ip)
{
    static const uint8_t code[] = { IL_OP_LDC_I4_3, IL_OP_LDC_I4, 0xFF };
    stack_slot_t before;

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    before = *stack_peek(&thread, 0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
    check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 1, 1);
    CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
}

TEST_CASE(unsupported_opcode_is_not_a_nop)
{
    static const uint8_t code[] = { 0x24 };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_UNSUPPORTED_OPCODE);
    check_fault(EXECUTOR_UNSUPPORTED_OPCODE, 0, 0);
}

TEST_CASE(falling_off_code_is_not_a_return)
{
    static const uint8_t code[] = { IL_OP_NOP };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_END_OF_CODE);
    check_fault(EXECUTOR_END_OF_CODE, sizeof code, 0);
}

TEST_CASE(stack_underflow_is_checked_for_each_operation)
{
    static const uint8_t operations[] = { IL_OP_DUP, IL_OP_POP, IL_OP_NEG,
                                        IL_OP_ADD, IL_OP_SUB, IL_OP_MUL };
    size_t index;

    for (index = 0; index < sizeof operations; index++) {
        uint8_t code[] = { operations[index] };

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_UNDERFLOW);
        check_fault(EXECUTOR_STACK_UNDERFLOW, 0, 0);
        if (operations[index] == IL_OP_ADD || operations[index] == IL_OP_SUB
            || operations[index] == IL_OP_MUL) {
            stack_slot_t before;

            start_thread(FRAMES);
            CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
            stack_push_i4(&thread, 9);
            before = *stack_peek(&thread, 0);
            CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_UNDERFLOW);
            check_fault(EXECUTOR_STACK_UNDERFLOW, 0, 1);
            CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
        }
    }
}

TEST_CASE(push_and_dup_cannot_exceed_max_stack)
{
    static const uint8_t operations[] = { IL_OP_LDC_I4_2, IL_OP_DUP };
    size_t index;

    for (index = 0; index < sizeof operations; index++) {
        uint8_t code[] = { IL_OP_LDC_I4_1, operations[index] };
        stack_slot_t before;

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_I4), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        before = *stack_peek(&thread, 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 1, 1);
        CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    }
}

TEST_CASE(frame_and_slot_limits_refuse_start_without_changes)
{
    static const uint8_t code[] = { IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, SLOTS + 1, STACK_K_EMPTY),
             EXECUTOR_STACK_OVERFLOW);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor.status, EXECUTOR_IDLE);
    check_stack();

    start_thread(0);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_STACK_OVERFLOW);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor.status, EXECUTOR_IDLE);
    check_stack();
}

TEST_CASE(arithmetic_rejects_non_i4_operands_without_consuming_them)
{
    static const uint8_t operations[] = { IL_OP_ADD, IL_OP_SUB, IL_OP_MUL, IL_OP_NEG };
    size_t index;

    for (index = 0; index < sizeof operations; index++) {
        uint8_t code[] = { operations[index] };
        stack_slot_t before[2];
        uint32_t depth;

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, sizeof code, 2, STACK_K_I4), EXECUTOR_CONTINUE);
        if (operations[index] != IL_OP_NEG)
            stack_push_i4(&thread, 4);
        stack_push_r8(&thread, 3.0);
        depth = stack_depth(&thread);
        memcpy(before, thread.frame->stack, depth * sizeof before[0]);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
        check_fault(EXECUTOR_TYPE_MISMATCH, 0, depth);
        CHECK(memcmp(before, thread.frame->stack, depth * sizeof before[0]) == 0);
    }
}

TEST_CASE(return_requires_the_exact_declared_depth)
{
    static const uint8_t missing[] = { IL_OP_RET };
    static const uint8_t extra[] = { IL_OP_LDC_I4_1, IL_OP_LDC_I4_2, IL_OP_RET };
    static const uint8_t void_value[] = { IL_OP_LDC_I4_1, IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(missing, sizeof missing, 0, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_RETURN);
    check_fault(EXECUTOR_BAD_RETURN, 0, 0);

    start_thread(FRAMES);
    CHECK_EQ(start_method(extra, sizeof extra, 2, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 3), EXECUTOR_BAD_RETURN);
    check_fault(EXECUTOR_BAD_RETURN, 2, 2);
    CHECK_EQ(stack_peek_i4(&thread, 0), 2);
    CHECK_EQ(stack_peek_i4(&thread, 1), 1);

    start_thread(FRAMES);
    CHECK_EQ(start_method(void_value, sizeof void_value, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_BAD_RETURN);
    check_fault(EXECUTOR_BAD_RETURN, 1, 1);
}

TEST_CASE(return_rejects_a_wrong_slot_kind)
{
    static const uint8_t code[] = { IL_OP_RET };
    stack_slot_t before;

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_I4), EXECUTOR_CONTINUE);
    stack_push_ref(&thread, OBJECT_NULL);
    before = *stack_peek(&thread, 0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
    check_fault(EXECUTOR_TYPE_MISMATCH, 0, 1);
    CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
}

TEST_CASE(invalid_arguments_and_idle_execution_are_refused)
{
    static const uint8_t code[] = { IL_OP_RET };
    executor_method_t method = { .code = code, .code_size = sizeof code,
                                 .method_id = 1, .return_kind = STACK_K_EMPTY };

    start_thread(FRAMES);
    CHECK_EQ(executor_step(NULL), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor_run(NULL, 0), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor_step(&executor), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor_run(&executor, 0), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor_start(NULL, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor_start(&executor, NULL, &method), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor_start(&executor, &thread, NULL), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(start_method(NULL, 1, 0, STACK_K_EMPTY), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(start_method(code, 0, 0, STACK_K_EMPTY), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_R8), EXECUTOR_INVALID_ARGUMENT);
    method.method_id = 0;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(executor.status, EXECUTOR_IDLE);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(stack_depth(&thread), 0);
    check_stack();
}

TEST_CASE(start_does_not_replace_an_active_execution_or_unconsumed_result)
{
    static const uint8_t code[] = { IL_OP_LDC_I4_2, IL_OP_RET };
    static const uint8_t other[] = { IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(start_method(other, sizeof other, 0, STACK_K_EMPTY), EXECUTOR_INVALID_ARGUMENT);
    CHECK(executor.method.code == code && thread.ip == code);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK_EQ(start_method(other, sizeof other, 0, STACK_K_EMPTY), EXECUTOR_INVALID_ARGUMENT);
    CHECK_EQ(stack_pop_i4(&thread), 2);
    CHECK_EQ(start_method(other, sizeof other, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
    check_stack();
}

TEST_CASE(ending_the_thread_invalidates_an_active_execution)
{
    static const uint8_t code[] = { IL_OP_NOP, IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    stack_thread_end(&thread);
    CHECK_EQ(executor_step(&executor), EXECUTOR_INVALID_STATE);
    CHECK_EQ(executor_run(&executor, 10), EXECUTOR_INVALID_STATE);
    CHECK_EQ(executor.offset, 0);
    CHECK(fake_oh.threads == NULL);
    check_stack();
}

TEST_CASE(contexts_execute_independently_on_the_same_object_handler)
{
    static const uint8_t first[] = { IL_OP_LDC_I4_2, IL_OP_LDC_I4_3, IL_OP_ADD, IL_OP_RET };
    static const uint8_t second[] = { IL_OP_LDC_I4_8, IL_OP_NEG, IL_OP_RET };
    stack_slot_t other_slots[SLOTS];
    stack_frame_t other_frames[FRAMES];
    stack_thread_t other_thread;
    executor_t other_executor;
    executor_method_t other_method = { .code = second, .code_size = sizeof second,
                                       .method_id = 2, .max_stack = 1,
                                       .return_kind = STACK_K_I4 };

    start_thread(FRAMES);
    memset(&other_thread, 0, sizeof other_thread);
    memset(&other_executor, 0, sizeof other_executor);
    CHECK(stack_thread_init(&fake_oh, &other_thread, other_slots, SLOTS, other_frames, FRAMES));
    CHECK_EQ(start_method(first, sizeof first, 2, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_start(&executor, &other_thread, &other_method), EXECUTOR_INVALID_ARGUMENT);
    CHECK(other_thread.frame == NULL && other_thread.ip == NULL);
    CHECK_EQ(executor_start(&other_executor, &other_thread, &other_method), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&other_executor, 3), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&other_thread), -8);
    CHECK_EQ(executor.offset, 1);
    CHECK(thread.ip == first + 1);
    CHECK_EQ(stack_peek_i4(&thread, 0), 2);
    CHECK_EQ(executor_run(&executor, 3), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 5);
    stack_thread_end(&other_thread);
    check_stack();
}

static executor_status_t start_typed_method(const uint8_t * code, uint32_t code_size,
                                            uint16_t max_stack, uint8_t return_kind,
                                            uint16_t argument_count, const uint8_t * argument_types,
                                            uint16_t local_count, const uint8_t * local_types)
{
    executor_method_t method = { .code = code, .code_size = code_size, .method_id = 1,
                                 .max_stack = max_stack, .return_kind = return_kind,
                                 .argument_count = argument_count, .local_count = local_count,
                                 .argument_types = argument_types, .local_types = local_types };

    return executor_start(&executor, &thread, &method);
}

TEST_CASE(arguments_and_locals_use_the_real_frame_windows)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
    static const uint8_t locals[] = { EXECUTOR_TYPE_INT32 };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_LDARG_1, IL_OP_ADD,
                                  IL_OP_STLOC_0, IL_OP_LDLOC_0, IL_OP_RET };

    start_thread(FRAMES);
    stack_push_i4(&thread, 2);
    stack_push_i4(&thread, 3);
    CHECK_EQ(start_typed_method(code, sizeof code, 2, STACK_K_I4, 2, arguments, 1, locals),
             EXECUTOR_CONTINUE);
    CHECK(thread.frame->args == slots);
    CHECK(thread.frame->locals == slots + 2);
    CHECK(thread.frame->stack == slots + 3);
    CHECK_EQ(thread.frame->locals[0].kind, STACK_K_I4);
    CHECK_EQ(thread.frame->locals[0].u.i4, 0);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor_run(&executor, 6), EXECUTOR_RETURNED);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(stack_depth(&thread), 1);
    CHECK_EQ(stack_pop_i4(&thread), 5);
    check_stack();
}

TEST_CASE(argument_entry_narrows_and_loads_use_declared_signedness)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT8, EXECUTOR_TYPE_UINT8 };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_LDARG_1, IL_OP_ADD, IL_OP_RET };

    start_thread(FRAMES);
    stack_push_i4(&thread, 0x1FF);
    stack_push_i4(&thread, 0x1FF);
    CHECK_EQ(start_typed_method(code, sizeof code, 2, STACK_K_I4, 2, arguments, 0, NULL),
             EXECUTOR_CONTINUE);
    CHECK_EQ(thread.frame->args[0].u.i4, 255);
    CHECK_EQ(thread.frame->args[1].u.i4, 255);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(stack_peek_i4(&thread, 0), -1);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(stack_peek_i4(&thread, 0), 255);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 254);
    check_stack();
}

TEST_CASE(local_store_narrows_before_a_widening_load)
{
    static const uint8_t locals[] = { EXECUTOR_TYPE_INT8 };
    static const uint8_t code[] = { IL_OP_LDC_I4_S, 0xFF, IL_OP_STLOC_0, IL_OP_LDLOC_0,
                                  IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 0, NULL, 1, locals),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    CHECK_EQ(thread.frame->locals[0].u.i4, 255);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), -1);
    check_stack();
}

TEST_CASE(compact_argument_loads_cover_all_four_indices)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32,
                                   EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
    uint32_t index;

    for (index = 0; index < 4; index++) {
        uint8_t code[] = { (uint8_t)(IL_OP_LDARG_0 + index), IL_OP_RET };
        uint32_t argument;

        start_thread(FRAMES);
        for (argument = 0; argument < 4; argument++)
            stack_push_i4(&thread, (int32_t)argument + 10);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 4, types, 0, NULL),
                 EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), index + 10);
        check_stack();
    }
}

TEST_CASE(compact_local_loads_and_stores_cover_all_four_indices)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32,
                                   EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
    uint32_t index;

    for (index = 0; index < 4; index++) {
        uint8_t code[] = { IL_OP_LDC_I4_S, (uint8_t)(index + 10),
                           (uint8_t)(IL_OP_STLOC_0 + index),
                           (uint8_t)(IL_OP_LDLOC_0 + index), IL_OP_RET };
        uint32_t local;

        start_thread(FRAMES);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 0, NULL, 4, types),
                 EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        for (local = 0; local < 4; local++)
            CHECK_EQ(thread.frame->locals[local].u.i4, local == index ? index + 10 : 0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), index + 10);
        check_stack();
    }
}

TEST_CASE(short_variable_forms_preserve_indices_and_instruction_lengths)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
    static const uint8_t code[] = {
        IL_OP_LDC_I4_7, IL_OP_STARG_S, 1, IL_OP_LDARG_S, 1, IL_OP_STLOC_S, 1,
        IL_OP_LDARGA_S, 1, IL_OP_POP, IL_OP_LDLOCA_S, 1, IL_OP_POP,
        IL_OP_LDLOC_S, 1, IL_OP_RET
    };
    object_byref_t address;

    start_thread(FRAMES);
    stack_push_i4(&thread, 10);
    stack_push_i4(&thread, 20);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 2, types, 2, types),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 7);
    CHECK_EQ(thread.frame->args[0].u.i4, 10);
    CHECK_EQ(thread.frame->args[1].u.i4, 7);
    CHECK_EQ(thread.frame->locals[0].u.i4, 0);
    CHECK_EQ(thread.frame->locals[1].u.i4, 7);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 9);
    address = stack_peek_byref(&thread, 0);
    CHECK_EQ(OBJECT_SPACE(address.id), OBJECT_SPACE_STACK);
    CHECK_EQ(OBJECT_INDEX(address.id), 1);
    CHECK(stack_byref_ptr(&thread, address) == &thread.frame->args[1].u);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 12);
    address = stack_peek_byref(&thread, 0);
    CHECK_EQ(OBJECT_INDEX(address.id), 3);
    CHECK(stack_byref_ptr(&thread, address) == &thread.frame->locals[1].u);
    CHECK_EQ(executor_run(&executor, 3), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 7);
    check_stack();
}

TEST_CASE(wide_variable_forms_use_indices_above_255)
{
    static stack_slot_t wide_slots[516];
    static stack_frame_t wide_frames[2];
    static const uint8_t code[] = {
        IL_OP_LDC_I4_7, 0xFE, IL_OP_FE_STARG, 0, 1,
        0xFE, IL_OP_FE_LDARG, 0, 1, 0xFE, IL_OP_FE_STLOC, 0, 1,
        0xFE, IL_OP_FE_LDARGA, 0, 1, IL_OP_POP,
        0xFE, IL_OP_FE_LDLOCA, 0, 1, IL_OP_POP,
        0xFE, IL_OP_FE_LDLOC, 0, 1, IL_OP_RET
    };
    uint8_t types[257];
    stack_thread_t wide_thread;
    executor_t wide_executor;
    executor_method_t method = { .code = code, .code_size = sizeof code,
                                 .method_id = 2, .max_stack = 1, .return_kind = STACK_K_I4,
                                 .argument_count = 257, .local_count = 257,
                                 .argument_types = types, .local_types = types };
    object_byref_t address;
    size_t index;

    start_thread(FRAMES);
    memset(types, EXECUTOR_TYPE_INT32, sizeof types);
    memset(wide_slots, GUARD, sizeof wide_slots);
    memset(wide_frames, GUARD, sizeof wide_frames);
    memset(&wide_thread, 0, sizeof wide_thread);
    memset(&wide_executor, 0, sizeof wide_executor);
    CHECK(stack_thread_init(&fake_oh, &wide_thread, wide_slots, 515, wide_frames, 1));
    for (index = 0; index < 257; index++)
        stack_push_i4(&wide_thread, index == 256 ? 99 : 0);
    CHECK_EQ(executor_start(&wide_executor, &wide_thread, &method), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&wide_executor, 4), EXECUTOR_CONTINUE);
    CHECK_EQ(wide_executor.offset, 13);
    CHECK_EQ(wide_thread.frame->args[0].u.i4, 0);
    CHECK_EQ(wide_thread.frame->args[256].u.i4, 7);
    CHECK_EQ(wide_thread.frame->locals[0].u.i4, 0);
    CHECK_EQ(wide_thread.frame->locals[256].u.i4, 7);
    CHECK_EQ(executor_step(&wide_executor), EXECUTOR_CONTINUE);
    address = stack_peek_byref(&wide_thread, 0);
    CHECK_EQ(OBJECT_INDEX(address.id), 256);
    CHECK(stack_byref_ptr(&wide_thread, address) == &wide_thread.frame->args[256].u);
    CHECK_EQ(executor_run(&wide_executor, 2), EXECUTOR_CONTINUE);
    address = stack_peek_byref(&wide_thread, 0);
    CHECK_EQ(OBJECT_INDEX(address.id), 513);
    CHECK(stack_byref_ptr(&wide_thread, address) == &wide_thread.frame->locals[256].u);
    CHECK_EQ(executor_run(&wide_executor, 3), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&wide_thread), 7);
    for (index = 0; index < sizeof wide_slots[515]; index++)
        CHECK_EQ(((const uint8_t *)&wide_slots[515])[index], GUARD);
    for (index = 0; index < sizeof wide_frames[1]; index++)
        CHECK_EQ(((const uint8_t *)&wide_frames[1])[index], GUARD);
    stack_thread_end(&wide_thread);
    check_stack();
}

TEST_CASE(each_declared_local_starts_as_a_typed_zero)
{
    static const struct {
        uint8_t type;
        uint8_t kind;
    } declared[] = {
        { EXECUTOR_TYPE_INT8, STACK_K_I4 }, { EXECUTOR_TYPE_UINT8, STACK_K_I4 },
        { EXECUTOR_TYPE_INT16, STACK_K_I4 }, { EXECUTOR_TYPE_UINT16, STACK_K_I4 },
        { EXECUTOR_TYPE_INT32, STACK_K_I4 }, { EXECUTOR_TYPE_UINT32, STACK_K_I4 },
        { EXECUTOR_TYPE_INT64, STACK_K_I8 }, { EXECUTOR_TYPE_UINT64, STACK_K_I8 },
        { EXECUTOR_TYPE_FLOAT32, STACK_K_R8 }, { EXECUTOR_TYPE_FLOAT64, STACK_K_R8 },
        { EXECUTOR_TYPE_NATIVE_INT, STACK_K_NINT }, { EXECUTOR_TYPE_NATIVE_UINT, STACK_K_NINT },
        { EXECUTOR_TYPE_REF, STACK_K_REF }, { EXECUTOR_TYPE_BYREF, STACK_K_BYREF }
    };
    static const uint8_t code[] = { IL_OP_LDLOC_0, IL_OP_POP, IL_OP_RET };
    static const uint8_t zero[8] = { 0 };
    size_t index;

    for (index = 0; index < sizeof declared / sizeof declared[0]; index++) {
        uint8_t types[] = { declared[index].type };

        start_thread(FRAMES);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 0, NULL, 1, types),
                 EXECUTOR_CONTINUE);
        CHECK_EQ(thread.frame->locals[0].kind, declared[index].kind);
        CHECK_EQ(thread.frame->locals[0].aux, 0);
        CHECK(memcmp(&thread.frame->locals[0].u, zero, sizeof zero) == 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(stack_peek(&thread, 0)->kind, declared[index].kind);
        CHECK(memcmp(&stack_peek(&thread, 0)->u, zero, sizeof zero) == 0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        CHECK_EQ(stack_depth(&thread), 0);
        check_stack();
    }
}

TEST_CASE(narrow_argument_and_local_stores_preserve_declared_signedness)
{
    static const struct {
        uint8_t type;
        uint32_t stored;
        int32_t loaded;
    } declared[] = {
        { EXECUTOR_TYPE_INT8, 255, -1 }, { EXECUTOR_TYPE_UINT8, 255, 255 },
        { EXECUTOR_TYPE_INT16, 65535, -1 }, { EXECUTOR_TYPE_UINT16, 65535, 65535 }
    };
    static const uint8_t code[] = { IL_OP_LDC_I4, 0xFF, 0xFF, 0x34, 0x12,
                                  IL_OP_STARG_S, 0, IL_OP_LDARG_0, IL_OP_STLOC_0,
                                  IL_OP_LDLOC_0, IL_OP_RET };
    size_t index;

    for (index = 0; index < sizeof declared / sizeof declared[0]; index++) {
        uint8_t types[] = { declared[index].type };

        start_thread(FRAMES);
        stack_push_i4(&thread, 0);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(thread.frame->args[0].u.i4, declared[index].stored);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(thread.frame->locals[0].u.i4, declared[index].stored);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), declared[index].loaded);
        check_stack();
    }
}

TEST_CASE(wider_numeric_locations_preserve_their_values)
{
    static const struct {
        uint8_t type;
        uint8_t kind;
    } declared[] = {
        { EXECUTOR_TYPE_INT32, STACK_K_I4 }, { EXECUTOR_TYPE_UINT32, STACK_K_I4 },
        { EXECUTOR_TYPE_INT64, STACK_K_I8 }, { EXECUTOR_TYPE_UINT64, STACK_K_I8 },
        { EXECUTOR_TYPE_FLOAT64, STACK_K_R8 },
        { EXECUTOR_TYPE_NATIVE_INT, STACK_K_NINT }, { EXECUTOR_TYPE_NATIVE_UINT, STACK_K_NINT }
    };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_STLOC_0, IL_OP_LDLOC_0,
                                  IL_OP_POP, IL_OP_RET };
    size_t index;

    for (index = 0; index < sizeof declared / sizeof declared[0]; index++) {
        uint8_t types[] = { declared[index].type };
        stack_slot_t value;

        start_thread(FRAMES);
        memset(&value, 0, sizeof value);
        value.kind = declared[index].kind;
        if (value.kind == STACK_K_I8)
            value.u.i8 = INT64_MIN + 12345;
        else if (value.kind == STACK_K_R8)
            value.u.r8 = 16777217.125;
        else if (value.kind == STACK_K_NINT)
            value.u.nint = INT32_MIN + 123;
        else
            value.u.i4 = INT32_MIN + 123;
        stack_push(&thread, &value);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        CHECK(memcmp(&thread.frame->args[0], &value, sizeof value) == 0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK(memcmp(&thread.frame->locals[0], &value, sizeof value) == 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK(memcmp(stack_peek(&thread, 0), &value, sizeof value) == 0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        check_stack();
    }
}

TEST_CASE(float32_arguments_are_stored_as_rounded_single_bytes)
{
    static const double values[] = { 3.25, 16777217.0, -16777217.0, 1.0 / 3.0, -0.0 };
    static const uint8_t types[] = { EXECUTOR_TYPE_FLOAT32 };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_POP, IL_OP_RET };
    size_t index;

    for (index = 0; index < sizeof values / sizeof values[0]; index++) {
        float expected = (float)values[index];

        start_thread(FRAMES);
        stack_push_r8(&thread, values[index]);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 1, types, 0, NULL),
                 EXECUTOR_CONTINUE);
        CHECK(memcmp(&thread.frame->args[0].u, &expected, sizeof expected) == 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(stack_peek(&thread, 0)->kind, STACK_K_R8);
        CHECK(stack_peek_r8(&thread, 0) == (double)expected);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        check_stack();
    }
}

TEST_CASE(float32_local_stores_round_then_load_as_r8)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_FLOAT64 };
    static const uint8_t locals[] = { EXECUTOR_TYPE_FLOAT32 };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_STLOC_0, IL_OP_LDLOC_0,
                                  IL_OP_STARG_S, 0, IL_OP_LDARG_0, IL_OP_POP, IL_OP_RET };
    float stored;

    start_thread(FRAMES);
    stack_push_r8(&thread, 16777217.0);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 1, arguments, 1, locals),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    memcpy(&stored, &thread.frame->locals[0].u, sizeof stored);
    CHECK(stored == 16777216.0f);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK(stack_peek_r8(&thread, 0) == 16777216.0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK(thread.frame->args[0].u.r8 == 16777216.0);
    CHECK_EQ(executor_run(&executor, 3), EXECUTOR_RETURNED);
    check_stack();
}

TEST_CASE(float32_argument_stores_round_without_changing_other_arguments)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_FLOAT32, EXECUTOR_TYPE_FLOAT64 };
    static const uint8_t code[] = { IL_OP_LDARG_1, IL_OP_STARG_S, 0, IL_OP_LDARG_0,
                                  IL_OP_POP, IL_OP_RET };
    float stored;

    start_thread(FRAMES);
    stack_push_r8(&thread, 4.0);
    stack_push_r8(&thread, 16777217.0);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 2, types, 0, NULL),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    memcpy(&stored, &thread.frame->args[0].u, sizeof stored);
    CHECK(stored == 16777216.0f);
    CHECK(thread.frame->args[1].u.r8 == 16777217.0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK(stack_peek_r8(&thread, 0) == 16777216.0);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    check_stack();
}

static object_id_t roots[16];
static uint32_t root_count;

static void record_root(obj_handler_t * oh, object_id_t id)
{
    CHECK(oh == &fake_oh);
    CHECK(root_count < sizeof roots / sizeof roots[0]);
    roots[root_count++] = id;
}

static void scan_roots(void)
{
    root_count = 0;
    fake_oh.root_scan(&fake_oh, record_root);
}

static uint32_t root_occurrences(object_id_t id)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < root_count; index++)
        count += roots[index] == id;
    return count;
}

TEST_CASE(reference_arguments_and_locals_remain_visible_to_root_scanning)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_REF, EXECUTOR_TYPE_REF };
    static const uint8_t locals[] = { EXECUTOR_TYPE_REF };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_STLOC_0, IL_OP_LDARG_1,
                                  IL_OP_STARG_S, 0, IL_OP_LDLOC_0, IL_OP_POP, IL_OP_RET };
    object_id_t first;
    object_id_t second;

    start_thread(FRAMES);
    first = objects_create(&fake_oh, FAKE_T_DATA, 4);
    second = objects_create(&fake_oh, FAKE_T_DATA, 4);
    CHECK(first != OBJECT_NULL && second != OBJECT_NULL);
    stack_push_ref(&thread, first);
    stack_push_ref(&thread, second);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 2, arguments, 1, locals),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    scan_roots();
    CHECK_EQ(root_occurrences(first), 2);
    CHECK_EQ(root_occurrences(second), 1);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    scan_roots();
    CHECK_EQ(root_occurrences(first), 1);
    CHECK_EQ(root_occurrences(second), 2);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(stack_peek_ref(&thread, 0), first);
    scan_roots();
    CHECK_EQ(root_occurrences(first), 2);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    scan_roots();
    CHECK_EQ(root_count, 0);
    objects_delete(&fake_oh, first);
    objects_delete(&fake_oh, second);
    check_stack();
}

TEST_CASE(byref_arguments_and_locals_preserve_base_and_offset)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_BYREF, EXECUTOR_TYPE_BYREF };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_STLOC_0, IL_OP_LDLOC_0,
                                  IL_OP_STARG_S, 1, IL_OP_LDARG_1, IL_OP_POP, IL_OP_RET };
    object_byref_t first = { OBJECT_ID(OBJECT_SPACE_STATIC, 8), 2, 0 };
    object_byref_t second = { OBJECT_ID(OBJECT_SPACE_STATIC, 16), 4, 0 };
    object_byref_t loaded;

    start_thread(FRAMES);
    stack_push_byref(&thread, first);
    stack_push_byref(&thread, second);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 2, types, 1, types),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 4), EXECUTOR_CONTINUE);
    CHECK_EQ(thread.frame->locals[0].u.byref_base, first.id);
    CHECK_EQ(thread.frame->locals[0].aux, first.offset);
    CHECK_EQ(thread.frame->args[1].u.byref_base, first.id);
    CHECK_EQ(thread.frame->args[1].aux, first.offset);
    scan_roots();
    CHECK_EQ(root_occurrences(first.id), 3);
    CHECK_EQ(root_occurrences(second.id), 0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    loaded = stack_peek_byref(&thread, 0);
    CHECK_EQ(loaded.id, first.id);
    CHECK_EQ(loaded.offset, first.offset);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    check_stack();
}

TEST_CASE(address_loads_expose_declared_storage_bytes)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT16 };
    static const uint8_t locals[] = { EXECUTOR_TYPE_UINT8, EXECUTOR_TYPE_FLOAT32 };
    static const uint8_t code[] = { IL_OP_LDARGA_S, 0, IL_OP_POP,
                                  IL_OP_LDLOCA_S, 0, IL_OP_POP,
                                  IL_OP_LDLOCA_S, 1, IL_OP_POP,
                                  IL_OP_LDARG_0, IL_OP_LDLOC_0, IL_OP_ADD,
                                  IL_OP_LDLOC_1, IL_OP_POP, IL_OP_RET };
    object_byref_t address;
    uint16_t signed_bytes = 0xFF80;
    uint8_t unsigned_bytes = 250;
    float float_bytes = 3.25f;

    start_thread(FRAMES);
    stack_push_i4(&thread, 0);
    CHECK_EQ(start_typed_method(code, sizeof code, 2, STACK_K_I4, 1, arguments, 2, locals),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    address = stack_peek_byref(&thread, 0);
    CHECK_EQ(OBJECT_INDEX(address.id), 0);
    memcpy(stack_byref_ptr(&thread, address), &signed_bytes, sizeof signed_bytes);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    address = stack_peek_byref(&thread, 0);
    CHECK_EQ(OBJECT_INDEX(address.id), 1);
    memcpy(stack_byref_ptr(&thread, address), &unsigned_bytes, sizeof unsigned_bytes);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    address = stack_peek_byref(&thread, 0);
    CHECK_EQ(OBJECT_INDEX(address.id), 2);
    memcpy(stack_byref_ptr(&thread, address), &float_bytes, sizeof float_bytes);
    CHECK_EQ(executor_run(&executor, 5), EXECUTOR_CONTINUE);
    CHECK(stack_peek_r8(&thread, 0) == 3.25);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 122);
    check_stack();
}

static const uint16_t variable_opcodes[] = {
    IL_OP_LDARG_0, IL_OP_LDARG_1, IL_OP_LDARG_2, IL_OP_LDARG_3,
    IL_OP_LDLOC_0, IL_OP_LDLOC_1, IL_OP_LDLOC_2, IL_OP_LDLOC_3,
    IL_OP_STLOC_0, IL_OP_STLOC_1, IL_OP_STLOC_2, IL_OP_STLOC_3,
    IL_OP_LDARG_S, IL_OP_LDARGA_S, IL_OP_STARG_S,
    IL_OP_LDLOC_S, IL_OP_LDLOCA_S, IL_OP_STLOC_S,
    0xFE00u | IL_OP_FE_LDARG, 0xFE00u | IL_OP_FE_LDARGA, 0xFE00u | IL_OP_FE_STARG,
    0xFE00u | IL_OP_FE_LDLOC, 0xFE00u | IL_OP_FE_LDLOCA, 0xFE00u | IL_OP_FE_STLOC
};

static uint32_t write_variable(uint8_t * code, uint16_t opcode, uint16_t index)
{
    if (opcode > 0xFFu) {
        code[0] = 0xFE;
        code[1] = (uint8_t)opcode;
        code[2] = (uint8_t)index;
        code[3] = (uint8_t)(index >> 8);
        return 4;
    }
    code[0] = (uint8_t)opcode;
    if (opcode >= IL_OP_LDARG_S && opcode <= IL_OP_STLOC_S) {
        code[1] = (uint8_t)index;
        return 2;
    }
    return 1;
}

TEST_CASE(entry_validates_all_type_metadata_and_argument_kinds_before_mutation)
{
    static const uint8_t code[] = { IL_OP_RET };
    static const uint8_t good[] = { EXECUTOR_TYPE_INT32 };
    static const uint8_t invalid[] = { 0 };
    static const uint8_t unknown[] = { EXECUTOR_TYPE_COUNT };
    static const uint8_t narrow[] = { EXECUTOR_TYPE_INT8 };
    executor_method_t method = { .code = code, .code_size = sizeof code,
                                 .method_id = 1, .return_kind = STACK_K_EMPTY,
                                 .argument_count = 1, .local_count = 1,
                                 .argument_types = good, .local_types = good };
    stack_slot_t before;

    start_thread(FRAMES);
    stack_push_i4(&thread, 0x1FF);
    before = *stack_peek(&thread, 0);
    method.argument_types = NULL;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.argument_types = invalid;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.argument_types = unknown;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.argument_types = narrow;
    method.local_types = NULL;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.local_types = invalid;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.local_types = unknown;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.local_types = good;
    method.argument_count = 0;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    method.argument_count = 2;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_INVALID_ARGUMENT);
    CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(executor.status, EXECUTOR_IDLE);
    check_stack();

    start_thread(FRAMES);
    stack_push_r8(&thread, 3.25);
    before = *stack_peek(&thread, 0);
    method.argument_count = 1;
    method.argument_types = good;
    CHECK_EQ(executor_start(&executor, &thread, &method), EXECUTOR_TYPE_MISMATCH);
    CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(executor.status, EXECUTOR_IDLE);
    check_stack();
}

TEST_CASE(entry_capacity_failure_does_not_narrow_supplied_arguments)
{
    static const uint8_t code[] = { IL_OP_RET };
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT8 };
    uint8_t locals[SLOTS - 1];
    stack_slot_t before;

    start_thread(FRAMES);
    memset(locals, EXECUTOR_TYPE_INT32, sizeof locals);
    stack_push_i4(&thread, 0x1FF);
    before = *stack_peek(&thread, 0);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY,
                                1, arguments, SLOTS - 1, locals), EXECUTOR_STACK_OVERFLOW);
    CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    CHECK_EQ(executor.status, EXECUTOR_IDLE);
    check_stack();

    start_thread(0);
    stack_push_i4(&thread, 0x1FF);
    CHECK_EQ(start_typed_method(code, sizeof code, 0, STACK_K_EMPTY, 1, arguments, 0, NULL),
             EXECUTOR_STACK_OVERFLOW);
    CHECK_EQ(stack_peek_i4(&thread, 0), 0x1FF);
    CHECK(thread.frame == NULL && thread.ip == NULL);
    check_stack();
}

TEST_CASE(every_variable_form_checks_indices_before_touching_storage)
{
    size_t index;

    for (index = 0; index < sizeof variable_opcodes / sizeof variable_opcodes[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, variable_opcodes[index], 0);
        stack_slot_t before;

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, code_size, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 7);
        before = *stack_peek(&thread, 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_INDEX);
        check_fault(EXECUTOR_BAD_INDEX, 0, 1);
        CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    }
}

TEST_CASE(variable_indices_at_the_count_or_uint16_limit_are_rejected)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32 };
    size_t index;

    for (index = 12; index < sizeof variable_opcodes / sizeof variable_opcodes[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, variable_opcodes[index],
                                             variable_opcodes[index] > 0xFFu ? UINT16_MAX : 1);
        stack_slot_t locations[2];
        stack_slot_t operand;

        start_thread(FRAMES);
        stack_push_i4(&thread, 9);
        CHECK_EQ(start_typed_method(code, code_size, 1, STACK_K_EMPTY, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        memcpy(locations, thread.frame->args, sizeof locations);
        stack_push_i4(&thread, 7);
        operand = *stack_peek(&thread, 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_INDEX);
        check_fault(EXECUTOR_BAD_INDEX, 0, 1);
        CHECK(memcmp(locations, thread.frame->args, sizeof locations) == 0);
        CHECK(memcmp(stack_peek(&thread, 0), &operand, sizeof operand) == 0);
    }
}

TEST_CASE(variable_operands_check_every_truncation_boundary)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32 };
    size_t index;

    for (index = 12; index < sizeof variable_opcodes / sizeof variable_opcodes[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, variable_opcodes[index], 0);
        uint32_t length;

        for (length = 1; length < code_size; length++) {
            stack_slot_t locations[2];
            stack_slot_t operand;

            start_thread(FRAMES);
            stack_push_i4(&thread, 9);
            CHECK_EQ(start_typed_method(code, length, 1, STACK_K_EMPTY, 1, types, 1, types),
                     EXECUTOR_CONTINUE);
            memcpy(locations, thread.frame->args, sizeof locations);
            stack_push_i4(&thread, 7);
            operand = *stack_peek(&thread, 0);
            CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
            check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 1);
            CHECK(memcmp(locations, thread.frame->args, sizeof locations) == 0);
            CHECK(memcmp(stack_peek(&thread, 0), &operand, sizeof operand) == 0);
        }
    }
}

TEST_CASE(variable_loads_and_addresses_honor_max_stack)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32 };
    static const uint16_t operations[] = {
        IL_OP_LDARG_0, IL_OP_LDLOC_0, IL_OP_LDARG_S, IL_OP_LDARGA_S,
        IL_OP_LDLOC_S, IL_OP_LDLOCA_S,
        0xFE00u | IL_OP_FE_LDARG, 0xFE00u | IL_OP_FE_LDARGA,
        0xFE00u | IL_OP_FE_LDLOC, 0xFE00u | IL_OP_FE_LDLOCA
    };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, operations[index], 0);
        stack_slot_t locations[2];

        start_thread(FRAMES);
        stack_push_i4(&thread, 9);
        CHECK_EQ(start_typed_method(code, code_size, 0, STACK_K_EMPTY, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        memcpy(locations, thread.frame->args, sizeof locations);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 0, 0);
        CHECK(memcmp(locations, thread.frame->args, sizeof locations) == 0);
    }
}

TEST_CASE(variable_stores_check_underflow_without_changing_locations)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32 };
    static const uint16_t operations[] = {
        IL_OP_STLOC_0, IL_OP_STLOC_S, IL_OP_STARG_S,
        0xFE00u | IL_OP_FE_STLOC, 0xFE00u | IL_OP_FE_STARG
    };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, operations[index], 0);
        stack_slot_t locations[2];

        start_thread(FRAMES);
        stack_push_i4(&thread, 9);
        CHECK_EQ(start_typed_method(code, code_size, 1, STACK_K_EMPTY, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        memcpy(locations, thread.frame->args, sizeof locations);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_UNDERFLOW);
        check_fault(EXECUTOR_STACK_UNDERFLOW, 0, 0);
        CHECK(memcmp(locations, thread.frame->args, sizeof locations) == 0);
    }
}

TEST_CASE(variable_stores_reject_wrong_kinds_without_consuming_operands)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32 };
    static const uint16_t operations[] = {
        IL_OP_STLOC_0, IL_OP_STLOC_S, IL_OP_STARG_S,
        0xFE00u | IL_OP_FE_STLOC, 0xFE00u | IL_OP_FE_STARG
    };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, operations[index], 0);
        stack_slot_t locations[2];
        stack_slot_t operand;

        start_thread(FRAMES);
        stack_push_i4(&thread, 9);
        CHECK_EQ(start_typed_method(code, code_size, 1, STACK_K_EMPTY, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        memcpy(locations, thread.frame->args, sizeof locations);
        stack_push_r8(&thread, 3.25);
        operand = *stack_peek(&thread, 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
        check_fault(EXECUTOR_TYPE_MISMATCH, 0, 1);
        CHECK(memcmp(locations, thread.frame->args, sizeof locations) == 0);
        CHECK(memcmp(stack_peek(&thread, 0), &operand, sizeof operand) == 0);
    }
}

TEST_CASE(variable_loads_reject_wrong_location_kinds)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_INT32 };
    static const uint8_t code[] = { IL_OP_LDLOC_0 };

    start_thread(FRAMES);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_EMPTY, 0, NULL, 1, types),
             EXECUTOR_CONTINUE);
    thread.frame->locals[0].kind = STACK_K_REF;
    CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
    check_fault(EXECUTOR_TYPE_MISMATCH, 0, 0);
    CHECK_EQ(thread.frame->locals[0].kind, STACK_K_REF);
}

TEST_CASE(addresses_of_byref_locations_are_outside_the_profile)
{
    static const uint8_t types[] = { EXECUTOR_TYPE_BYREF };
    static const uint16_t operations[] = { IL_OP_LDARGA_S, IL_OP_LDLOCA_S,
                                          0xFE00u | IL_OP_FE_LDARGA,
                                          0xFE00u | IL_OP_FE_LDLOCA };
    object_byref_t address = { OBJECT_ID(OBJECT_SPACE_STATIC, 8), 2, 0 };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[4];
        uint32_t code_size = write_variable(code, operations[index], 0);
        stack_slot_t locations[2];

        start_thread(FRAMES);
        stack_push_byref(&thread, address);
        CHECK_EQ(start_typed_method(code, code_size, 1, STACK_K_EMPTY, 1, types, 1, types),
                 EXECUTOR_CONTINUE);
        memcpy(locations, thread.frame->args, sizeof locations);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
        check_fault(EXECUTOR_TYPE_MISMATCH, 0, 0);
        CHECK(memcmp(locations, thread.frame->args, sizeof locations) == 0);
    }
}

TEST_CASE(unsupported_prefixed_opcode_is_not_a_variable_instruction)
{
    static const uint8_t code[] = { 0xFE, 0xFF };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_UNSUPPORTED_OPCODE);
    check_fault(EXECUTOR_UNSUPPORTED_OPCODE, 0, 0);
}

TEST_CASE(short_and_long_branches_are_relative_to_the_instruction_end)
{
    static const uint8_t short_code[] = { IL_OP_BR_S, 1, IL_OP_NOP, IL_OP_LDC_I4_5,
                                         IL_OP_RET };
    static const uint8_t long_code[] = { IL_OP_BR, 1, 0, 0, 0, IL_OP_NOP,
                                        IL_OP_LDC_I4_5, IL_OP_RET };

    CHECK_EQ(run_i4(short_code, sizeof short_code, 1), 5);
    CHECK_EQ(run_i4(long_code, sizeof long_code, 1), 5);
}

TEST_CASE(truth_branches_consume_the_condition_only_after_validation)
{
    static const uint8_t code[] = { IL_OP_LDC_I4_0, IL_OP_BRFALSE_S, 1,
                                  IL_OP_LDC_I4_1, IL_OP_LDC_I4_5, IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 4);
    CHECK(thread.ip == code + 4);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 5);
    check_stack();
}

TEST_CASE(comparison_opcodes_produce_typed_boolean_results)
{
    static const struct {
        uint8_t opcode;
        int32_t left;
        int32_t right;
        int32_t result;
    } comparisons[] = {
        { IL_OP_FE_CEQ, 3, 3, 1 }, { IL_OP_FE_CEQ, 2, 3, 0 },
        { IL_OP_FE_CGT, 2, 3, 0 }, { IL_OP_FE_CGT, 3, 2, 1 },
        { IL_OP_FE_CLT, 2, 3, 1 }, { IL_OP_FE_CLT, 3, 2, 0 },
        { IL_OP_FE_CGT, -1, 0, 0 }, { IL_OP_FE_CGT_UN, -1, 0, 1 },
        { IL_OP_FE_CLT, -1, 0, 1 }, { IL_OP_FE_CLT_UN, -1, 0, 0 }
    };
    size_t index;

    for (index = 0; index < sizeof comparisons / sizeof comparisons[0]; index++) {
        uint8_t code[13];

        write_i4(code, comparisons[index].left);
        write_i4(code + 5, comparisons[index].right);
        code[10] = 0xFE;
        code[11] = comparisons[index].opcode;
        code[12] = IL_OP_RET;
        CHECK_EQ(run_i4(code, sizeof code, 2), comparisons[index].result);
    }
}

TEST_CASE(backward_branch_respects_and_resumes_instruction_budgets)
{
    static const uint8_t code[] = { IL_OP_BR_S, 0xFE };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 7), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    CHECK(thread.ip == code);
    CHECK_EQ(executor_run(&executor, 3), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    CHECK_EQ(stack_depth(&thread), 0);
    check_stack();
}

TEST_CASE(branches_cannot_target_literal_operand_bytes)
{
    static const uint8_t code[] = { IL_OP_BR_S, 1, IL_OP_LDC_I4, 5, 0, 0, 0,
                                  IL_OP_RET };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_I4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_BRANCH_TARGET);
    check_fault(EXECUTOR_BAD_BRANCH_TARGET, 0, 0);
}

static uint32_t write_branch(uint8_t * code, uint16_t opcode, int32_t displacement)
{
    uint32_t bits = (uint32_t)displacement;

    code[0] = (uint8_t)opcode;
    if (opcode >= IL_OP_BR_S && opcode <= IL_OP_BLT_UN_S) {
        code[1] = (uint8_t)bits;
        return 2;
    }
    code[1] = (uint8_t)bits;
    code[2] = (uint8_t)(bits >> 8);
    code[3] = (uint8_t)(bits >> 16);
    code[4] = (uint8_t)(bits >> 24);
    return 5;
}

static uint8_t slot_type(uint8_t kind)
{
    switch (kind) {
    case STACK_K_I4: return EXECUTOR_TYPE_INT32;
    case STACK_K_I8: return EXECUTOR_TYPE_INT64;
    case STACK_K_R8: return EXECUTOR_TYPE_FLOAT64;
    case STACK_K_NINT: return EXECUTOR_TYPE_NATIVE_INT;
    case STACK_K_REF: return EXECUTOR_TYPE_REF;
    case STACK_K_BYREF: return EXECUTOR_TYPE_BYREF;
    default: return EXECUTOR_TYPE_COUNT;
    }
}

static int32_t run_pair_operation(uint16_t opcode, const stack_slot_t * left,
                                  const stack_slot_t * right)
{
    uint8_t code[16] = { IL_OP_LDARG_0, IL_OP_LDARG_1 };
    uint8_t types[] = { slot_type(left->kind), slot_type(right->kind) };
    uint32_t code_size;
    int32_t result;

    if (opcode > 0xFFu) {
        code[2] = IL_OP_PREFIX1;
        code[3] = (uint8_t)opcode;
        code[4] = IL_OP_RET;
        code_size = 5;
    } else {
        uint32_t base = 2 + write_branch(code + 2, opcode, 2);

        code[base] = IL_OP_LDC_I4_0;
        code[base + 1] = IL_OP_RET;
        code[base + 2] = IL_OP_LDC_I4_1;
        code[base + 3] = IL_OP_RET;
        code_size = base + 4;
    }
    start_thread(FRAMES);
    stack_push(&thread, left);
    stack_push(&thread, right);
    CHECK_EQ(start_typed_method(code, code_size, 2, STACK_K_I4, 2, types, 0, NULL),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, code_size), EXECUTOR_RETURNED);
    result = stack_pop_i4(&thread);
    check_stack();
    return result;
}

static int32_t run_truth_operation(uint16_t opcode, const stack_slot_t * condition)
{
    uint8_t code[12] = { IL_OP_LDARG_0 };
    uint8_t types[] = { slot_type(condition->kind) };
    uint32_t base = 1 + write_branch(code + 1, opcode, 2);
    int32_t result;

    code[base] = IL_OP_LDC_I4_0;
    code[base + 1] = IL_OP_RET;
    code[base + 2] = IL_OP_LDC_I4_1;
    code[base + 3] = IL_OP_RET;
    start_thread(FRAMES);
    stack_push(&thread, condition);
    CHECK_EQ(start_typed_method(code, base + 4, 1, STACK_K_I4, 1, types, 0, NULL),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, base + 4), EXECUTOR_RETURNED);
    result = stack_pop_i4(&thread);
    check_stack();
    return result;
}

static int expected_integer_branch(uint16_t opcode, int32_t left, int32_t right)
{
    switch (opcode) {
    case IL_OP_BEQ_S: return left == right;
    case IL_OP_BGE_S: return left >= right;
    case IL_OP_BGT_S: return left > right;
    case IL_OP_BLE_S: return left <= right;
    case IL_OP_BLT_S: return left < right;
    case IL_OP_BNE_UN_S: return left != right;
    case IL_OP_BGE_UN_S: return (uint32_t)left >= (uint32_t)right;
    case IL_OP_BGT_UN_S: return (uint32_t)left > (uint32_t)right;
    case IL_OP_BLE_UN_S: return (uint32_t)left <= (uint32_t)right;
    case IL_OP_BLT_UN_S: return (uint32_t)left < (uint32_t)right;
    default: return -1;
    }
}

TEST_CASE(all_binary_branches_cover_equal_less_and_greater_outcomes)
{
    static const int32_t values[] = { INT32_MIN, -1, 0, 1, INT32_MAX };
    uint16_t opcode;
    size_t left_index;
    size_t right_index;
    uint16_t form;

    for (opcode = IL_OP_BEQ_S; opcode <= IL_OP_BLT_UN_S; opcode++)
        for (form = 0; form < 2; form++)
            for (left_index = 0; left_index < sizeof values / sizeof values[0]; left_index++)
                for (right_index = 0; right_index < sizeof values / sizeof values[0]; right_index++) {
                    stack_slot_t left;
                    stack_slot_t right;
                    uint16_t encoded = form == 0 ? opcode : opcode + IL_OP_BR - IL_OP_BR_S;

                    memset(&left, 0, sizeof left);
                    memset(&right, 0, sizeof right);
                    left.kind = right.kind = STACK_K_I4;
                    left.u.i4 = values[left_index];
                    right.u.i4 = values[right_index];
                    CHECK_EQ(run_pair_operation(encoded, &left, &right),
                             expected_integer_branch(opcode, left.u.i4, right.u.i4));
                }
}

TEST_CASE(comparisons_support_i8_and_native_integer_pairs)
{
    static const uint8_t kinds[] = { STACK_K_I8, STACK_K_NINT };
    size_t index;

    for (index = 0; index < sizeof kinds; index++) {
        stack_slot_t left;
        stack_slot_t right;

        memset(&left, 0, sizeof left);
        memset(&right, 0, sizeof right);
        left.kind = right.kind = kinds[index];
        if (left.kind == STACK_K_I8)
            left.u.i8 = -1;
        else
            left.u.nint = -1;
        CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CEQ, &left, &right), 0);
        CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CLT, &left, &right), 1);
        CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT_UN, &left, &right), 1);
        CHECK_EQ(run_pair_operation(IL_OP_BLT, &left, &right), 1);
        CHECK_EQ(run_pair_operation(IL_OP_BGT_UN, &left, &right), 1);
    }
}

TEST_CASE(i8_comparisons_do_not_truncate_to_32_bits)
{
    stack_slot_t left;
    stack_slot_t right;

    memset(&left, 0, sizeof left);
    memset(&right, 0, sizeof right);
    left.kind = right.kind = STACK_K_I8;
    left.u.i8 = (int64_t)UINT32_MAX + 1;
    right.u.i8 = 0;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CEQ, &left, &right), 0);
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT, &left, &right), 1);
    CHECK_EQ(run_pair_operation(IL_OP_BGT_S, &left, &right), 1);
    left.u.i8 = INT64_MIN;
    right.u.i8 = INT64_MAX;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CLT, &left, &right), 1);
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT_UN, &left, &right), 1);
}

TEST_CASE(mixed_i4_native_comparisons_preserve_32_bit_signedness)
{
    uint8_t reverse;

    for (reverse = 0; reverse < 2; reverse++) {
        stack_slot_t left;
        stack_slot_t right;

        memset(&left, 0, sizeof left);
        memset(&right, 0, sizeof right);
        left.kind = reverse ? STACK_K_NINT : STACK_K_I4;
        right.kind = reverse ? STACK_K_I4 : STACK_K_NINT;
        if (left.kind == STACK_K_I4)
            left.u.i4 = -1;
        else
            left.u.nint = -1;
        CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CLT, &left, &right), 1);
        CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT_UN, &left, &right), 1);
        CHECK_EQ(run_pair_operation(IL_OP_BGE_UN_S, &left, &right), 1);
        if (right.kind == STACK_K_I4)
            right.u.i4 = -1;
        else
            right.u.nint = -1;
        CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CEQ, &left, &right), 1);
        CHECK_EQ(run_pair_operation(IL_OP_BEQ, &left, &right), 1);
    }
}

TEST_CASE(nan_comparisons_distinguish_ordered_and_unordered_forms)
{
    static const struct {
        uint8_t opcode;
        int expected;
    } operations[] = {
        { IL_OP_FE_CEQ, 0 }, { IL_OP_FE_CGT, 0 }, { IL_OP_FE_CLT, 0 },
        { IL_OP_FE_CGT_UN, 1 }, { IL_OP_FE_CLT_UN, 1 }
    };
    uint8_t side;
    size_t index;

    for (side = 0; side < 3; side++) {
        stack_slot_t left;
        stack_slot_t right;

        memset(&left, 0, sizeof left);
        memset(&right, 0, sizeof right);
        left.kind = right.kind = STACK_K_R8;
        left.u.r8 = side == 1 ? 3.0 : NAN;
        right.u.r8 = side == 0 ? 3.0 : NAN;
        for (index = 0; index < sizeof operations / sizeof operations[0]; index++)
            CHECK_EQ(run_pair_operation(0xFE00u | operations[index].opcode, &left, &right),
                     operations[index].expected);
    }
}

TEST_CASE(nan_binary_branches_follow_unordered_semantics_in_both_forms)
{
    uint16_t opcode;
    uint16_t form;
    stack_slot_t left;
    stack_slot_t right;

    memset(&left, 0, sizeof left);
    memset(&right, 0, sizeof right);
    left.kind = right.kind = STACK_K_R8;
    left.u.r8 = NAN;
    right.u.r8 = 3.0;
    for (opcode = IL_OP_BEQ_S; opcode <= IL_OP_BLT_UN_S; opcode++)
        for (form = 0; form < 2; form++) {
            uint16_t encoded = form == 0 ? opcode : opcode + IL_OP_BR - IL_OP_BR_S;

            CHECK_EQ(run_pair_operation(encoded, &left, &right), opcode >= IL_OP_BNE_UN_S);
        }
}

TEST_CASE(floating_zero_and_infinity_compare_without_integer_conversion)
{
    stack_slot_t left;
    stack_slot_t right;

    memset(&left, 0, sizeof left);
    memset(&right, 0, sizeof right);
    left.kind = right.kind = STACK_K_R8;
    left.u.r8 = -0.0;
    right.u.r8 = 0.0;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CEQ, &left, &right), 1);
    CHECK_EQ(run_pair_operation(IL_OP_BEQ_S, &left, &right), 1);
    CHECK_EQ(run_pair_operation(IL_OP_BGE_UN, &left, &right), 1);
    left.u.r8 = INFINITY;
    right.u.r8 = -INFINITY;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT, &left, &right), 1);
    CHECK_EQ(run_pair_operation(IL_OP_BGT_S, &left, &right), 1);
    CHECK_EQ(run_pair_operation(IL_OP_BLT_UN, &right, &left), 1);
    left.u.r8 = 0.125;
    right.u.r8 = 0.25;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CLT_UN, &left, &right), 1);
}

TEST_CASE(reference_identity_and_null_comparisons_do_not_resolve_payloads)
{
    object_id_t first;
    object_id_t second;
    stack_slot_t left;
    stack_slot_t right;

    start_thread(FRAMES);
    first = objects_create(&fake_oh, FAKE_T_DATA, 4);
    second = objects_create(&fake_oh, FAKE_T_DATA, 4);
    CHECK(first != OBJECT_NULL && second != OBJECT_NULL);
    memset(&left, 0, sizeof left);
    memset(&right, 0, sizeof right);
    left.kind = right.kind = STACK_K_REF;
    left.u.id = right.u.id = first;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CEQ, &left, &right), 1);
    CHECK_EQ(run_pair_operation(IL_OP_BEQ, &left, &right), 1);
    right.u.id = second;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CEQ, &left, &right), 0);
    CHECK_EQ(run_pair_operation(IL_OP_BNE_UN_S, &left, &right), 1);
    right.u.id = OBJECT_NULL;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT_UN, &left, &right), 1);
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT_UN, &right, &left), 0);
    left.u.id = OBJECT_NULL;
    CHECK_EQ(run_pair_operation(0xFE00u | IL_OP_FE_CGT_UN, &left, &right), 0);
    CHECK_EQ(run_pair_operation(IL_OP_BEQ_S, &left, &right), 1);
    objects_delete(&fake_oh, first);
    objects_delete(&fake_oh, second);
    check_stack();
}

TEST_CASE(truth_branches_support_each_zero_and_nonzero_operand_kind)
{
    static const uint8_t kinds[] = { STACK_K_I4, STACK_K_I8, STACK_K_NINT,
                                    STACK_K_REF, STACK_K_BYREF };
    size_t index;
    int truth;

    for (index = 0; index < sizeof kinds; index++)
        for (truth = 0; truth < 2; truth++) {
            stack_slot_t condition;

            memset(&condition, 0, sizeof condition);
            condition.kind = kinds[index];
            if (condition.kind == STACK_K_I8)
                condition.u.i8 = truth ? INT64_MIN : 0;
            else if (condition.kind == STACK_K_I4)
                condition.u.i4 = truth ? INT32_MIN : 0;
            else if (condition.kind == STACK_K_NINT)
                condition.u.nint = truth ? -1 : 0;
            else
                condition.u.id = truth ? OBJECT_ID(OBJECT_SPACE_STATIC, 8) : OBJECT_NULL;
            CHECK_EQ(run_truth_operation(IL_OP_BRTRUE_S, &condition), truth);
            CHECK_EQ(run_truth_operation(IL_OP_BRTRUE, &condition), truth);
            CHECK_EQ(run_truth_operation(IL_OP_BRFALSE_S, &condition), !truth);
            CHECK_EQ(run_truth_operation(IL_OP_BRFALSE, &condition), !truth);
        }
}

TEST_CASE(switch_cases_use_the_end_of_the_whole_table_as_their_base)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32 };
    static const uint8_t code[] = {
        IL_OP_LDARG_0, IL_OP_SWITCH, 2, 0, 0, 0,
        2, 0, 0, 0, 4, 0, 0, 0,
        IL_OP_LDC_I4_0, IL_OP_RET, IL_OP_LDC_I4_1, IL_OP_RET, IL_OP_LDC_I4_2, IL_OP_RET
    };
    static const struct {
        int32_t selected;
        int32_t expected;
        uint32_t destination;
    } selections[] = { { 0, 1, 16 }, { 1, 2, 18 }, { 2, 0, 14 },
                       { -1, 0, 14 }, { INT32_MAX, 0, 14 } };
    size_t index;

    for (index = 0; index < sizeof selections / sizeof selections[0]; index++) {
        start_thread(FRAMES);
        stack_push_i4(&thread, selections[index].selected);
        CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 1, arguments, 0, NULL),
                 EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.offset, selections[index].destination);
        CHECK(thread.ip == code + selections[index].destination);
        CHECK_EQ(stack_depth(&thread), 0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), selections[index].expected);
        check_stack();
    }
}

TEST_CASE(empty_switch_consumes_the_index_and_falls_through)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32 };
    static const uint8_t code[] = { IL_OP_LDARG_0, IL_OP_SWITCH, 0, 0, 0, 0,
                                  IL_OP_LDC_I4_5, IL_OP_RET };

    start_thread(FRAMES);
    stack_push_i4(&thread, -1);
    CHECK_EQ(start_typed_method(code, sizeof code, 1, STACK_K_I4, 1, arguments, 0, NULL),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 6);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 5);
    check_stack();
}

TEST_CASE(backward_switch_obeys_the_instruction_budget)
{
    static const uint8_t code[] = { IL_OP_LDC_I4_0, IL_OP_SWITCH, 1, 0, 0, 0,
                                  0xF6, 0xFF, 0xFF, 0xFF };

    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 8), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    CHECK_EQ(stack_depth(&thread), 0);
    CHECK_EQ(executor_run(&executor, 1), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 1);
    CHECK_EQ(stack_peek_i4(&thread, 0), 0);
    CHECK_EQ(executor_run(&executor, 1), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    check_stack();
}

TEST_CASE(long_backward_displacements_are_not_narrowed_to_a_signed_byte)
{
    uint8_t code[131];

    memset(code, IL_OP_NOP, sizeof code);
    write_branch(code, IL_OP_BR, 120);
    write_branch(code + 125, IL_OP_BR, -130);
    code[130] = IL_OP_RET;
    start_thread(FRAMES);
    CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 125);
    CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    CHECK_EQ(executor_run(&executor, 4), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 0);
    check_stack();
}

TEST_CASE(loop_with_arguments_and_locals_can_resume_and_return_a_sum)
{
    static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32 };
    static const uint8_t locals[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
    static const uint8_t code[] = {
        IL_OP_LDC_I4_0, IL_OP_STLOC_0, IL_OP_LDC_I4_0, IL_OP_STLOC_1,
        IL_OP_LDLOC_1, IL_OP_LDARG_0, IL_OP_BGE_S, 10,
        IL_OP_LDLOC_0, IL_OP_LDLOC_1, IL_OP_ADD, IL_OP_STLOC_0,
        IL_OP_LDLOC_1, IL_OP_LDC_I4_1, IL_OP_ADD, IL_OP_STLOC_1,
        IL_OP_BR_S, 0xF2, IL_OP_LDLOC_0, IL_OP_RET
    };
    uint32_t budget_count;

    start_thread(FRAMES);
    stack_push_i4(&thread, 5);
    CHECK_EQ(start_typed_method(code, sizeof code, 2, STACK_K_I4, 1, arguments, 2, locals),
             EXECUTOR_CONTINUE);
    CHECK_EQ(executor_run(&executor, 5), EXECUTOR_CONTINUE);
    CHECK_EQ(executor.offset, 5);
    CHECK_EQ(stack_depth(&thread), 1);
    for (budget_count = 0; budget_count < 30 && executor.status == EXECUTOR_CONTINUE; budget_count++) {
        executor_run(&executor, 3);
        check_stack();
    }
    CHECK_EQ(executor.status, EXECUTOR_RETURNED);
    CHECK_EQ(stack_pop_i4(&thread), 10);
    check_stack();
}

TEST_CASE(branch_operands_check_every_truncation_boundary)
{
    uint16_t opcode;

    for (opcode = IL_OP_BR_S; opcode <= IL_OP_BLT_UN; opcode++) {
        uint8_t code[6];
        uint32_t length = write_branch(code, opcode, 0);
        uint32_t available;

        for (available = 1; available < length; available++) {
            stack_slot_t before[2];

            start_thread(FRAMES);
            CHECK_EQ(start_method(code, available, 2, STACK_K_EMPTY), EXECUTOR_CONTINUE);
            stack_push_i4(&thread, 7);
            stack_push_i4(&thread, 8);
            memcpy(before, thread.frame->stack, sizeof before);
            CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
            check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 2);
            CHECK(memcmp(before, thread.frame->stack, sizeof before) == 0);
        }
    }
}

TEST_CASE(comparison_prefix_truncation_preserves_operands)
{
    uint8_t opcode;

    for (opcode = IL_OP_FE_CEQ; opcode <= IL_OP_FE_CLT_UN; opcode++) {
        uint8_t code[] = { IL_OP_PREFIX1, opcode };
        stack_slot_t before[2];

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, 1, 2, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 7);
        stack_push_i4(&thread, 8);
        memcpy(before, thread.frame->stack, sizeof before);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
        check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 2);
        CHECK(memcmp(before, thread.frame->stack, sizeof before) == 0);
    }
}

TEST_CASE(switch_headers_and_tables_are_bounded_before_operand_reads)
{
    static const uint8_t code[] = { IL_OP_SWITCH, 2, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t overflowing[] = { IL_OP_SWITCH, 0xFF, 0xFF, 0xFF, 0xFF };
    uint32_t available;

    for (available = 1; available < sizeof code; available++) {
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, available, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
        check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 0);
    }
    start_thread(FRAMES);
    CHECK_EQ(start_method(overflowing, sizeof overflowing, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
    stack_push_i4(&thread, 0);
    CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
    check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 1);
}

TEST_CASE(out_of_method_branch_targets_do_not_change_state)
{
    static const struct {
        uint16_t opcode;
        int32_t displacement;
    } branches[] = {
        { IL_OP_BR_S, -3 }, { IL_OP_BR_S, 1 }, { IL_OP_BR_S, -128 }, { IL_OP_BR_S, 127 },
        { IL_OP_BR, -6 }, { IL_OP_BR, 1 }, { IL_OP_BR, INT32_MIN }, { IL_OP_BR, INT32_MAX }
    };
    size_t index;

    for (index = 0; index < sizeof branches / sizeof branches[0]; index++) {
        uint8_t code[6];
        uint32_t length = write_branch(code, branches[index].opcode, branches[index].displacement);
        stack_slot_t before[2];

        code[length] = IL_OP_RET;
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, length + 1, 2, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 7);
        stack_push_i4(&thread, 8);
        memcpy(before, thread.frame->stack, sizeof before);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_BRANCH_TARGET);
        check_fault(EXECUTOR_BAD_BRANCH_TARGET, 0, 2);
        CHECK(memcmp(before, thread.frame->stack, sizeof before) == 0);
    }
}

TEST_CASE(targets_cannot_enter_prefixes_indices_or_switch_tables)
{
    static const uint8_t prefix[] = { IL_OP_BR_S, 1, IL_OP_PREFIX1, IL_OP_FE_CEQ, IL_OP_RET };
    static const uint8_t short_index[] = { IL_OP_BR_S, 1, IL_OP_LDARG_S, 0, IL_OP_RET };
    static const uint8_t wide_index[] = { IL_OP_BR_S, 2, IL_OP_PREFIX1, IL_OP_FE_LDARG,
                                         0, 0, IL_OP_RET };
    static const uint8_t switch_entry[] = { IL_OP_BR_S, 5, IL_OP_SWITCH, 1, 0, 0, 0,
                                           0, 0, 0, 0, IL_OP_RET };
    static const uint8_t unsupported[] = { IL_OP_BR_S, 1, 0x24, IL_OP_RET };
    static const uint8_t truncated[] = { IL_OP_BR_S, 2, IL_OP_LDC_I4, 0, IL_OP_RET };
    static const struct {
        const uint8_t * code;
        uint32_t size;
    } programs[] = {
        { prefix, sizeof prefix }, { short_index, sizeof short_index },
        { wide_index, sizeof wide_index }, { switch_entry, sizeof switch_entry },
        { unsupported, sizeof unsupported }, { truncated, sizeof truncated }
    };
    size_t index;

    for (index = 0; index < sizeof programs / sizeof programs[0]; index++) {
        start_thread(FRAMES);
        CHECK_EQ(start_method(programs[index].code, programs[index].size, 0, STACK_K_EMPTY),
                 EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_BRANCH_TARGET);
        check_fault(EXECUTOR_BAD_BRANCH_TARGET, 0, 0);
    }
}

TEST_CASE(untaken_truth_branches_still_validate_their_destinations)
{
    static const uint16_t operations[] = { IL_OP_BRTRUE_S, IL_OP_BRTRUE,
                                          IL_OP_BRFALSE_S, IL_OP_BRFALSE };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[6];
        uint32_t length = write_branch(code, operations[index], 1);
        int32_t condition = operations[index] == IL_OP_BRFALSE_S
                            || operations[index] == IL_OP_BRFALSE;

        code[length] = IL_OP_RET;
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, length + 1, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, condition);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_BRANCH_TARGET);
        check_fault(EXECUTOR_BAD_BRANCH_TARGET, 0, 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), condition);
    }
}

TEST_CASE(unselected_switch_destinations_are_not_ignored)
{
    static const uint8_t code[] = { IL_OP_SWITCH, 2, 0, 0, 0, 0, 0, 0, 0,
                                  0xFF, 0xFF, 0xFF, 0x7F, IL_OP_RET };
    static const int32_t indices[] = { 0, -1 };
    size_t index;

    for (index = 0; index < sizeof indices / sizeof indices[0]; index++) {
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, sizeof code, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, indices[index]);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_BRANCH_TARGET);
        check_fault(EXECUTOR_BAD_BRANCH_TARGET, 0, 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), indices[index]);
    }
}

TEST_CASE(comparisons_and_binary_branches_check_depth_without_consuming_values)
{
    uint16_t index;
    uint32_t depth;

    for (index = 0; index < 25; index++) {
        uint8_t code[6];
        uint32_t length;

        if (index < 5) {
            code[0] = IL_OP_PREFIX1;
            code[1] = (uint8_t)(IL_OP_FE_CEQ + index);
            length = 2;
        } else {
            uint16_t opcode = IL_OP_BEQ_S + (index - 5) % 10;

            if (index >= 15)
                opcode += IL_OP_BR - IL_OP_BR_S;
            length = write_branch(code, opcode, 0);
        }
        code[length] = IL_OP_RET;
        for (depth = 0; depth < 2; depth++) {
            start_thread(FRAMES);
            CHECK_EQ(start_method(code, length + 1, 2, STACK_K_EMPTY), EXECUTOR_CONTINUE);
            if (depth != 0)
                stack_push_i4(&thread, 7);
            CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_UNDERFLOW);
            check_fault(EXECUTOR_STACK_UNDERFLOW, 0, depth);
            if (depth != 0)
                CHECK_EQ(stack_peek_i4(&thread, 0), 7);
        }
    }
}

TEST_CASE(truth_branches_and_switch_check_underflow_and_operand_kinds)
{
    static const uint16_t operations[] = { IL_OP_BRTRUE_S, IL_OP_BRTRUE,
                                          IL_OP_BRFALSE_S, IL_OP_BRFALSE, IL_OP_SWITCH };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[6] = { IL_OP_SWITCH, 0, 0, 0, 0, IL_OP_RET };
        uint32_t length = operations[index] == IL_OP_SWITCH
                          ? 5 : write_branch(code, operations[index], 0);
        stack_slot_t before;

        code[length] = IL_OP_RET;
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, length + 1, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_UNDERFLOW);
        check_fault(EXECUTOR_STACK_UNDERFLOW, 0, 0);
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, length + 1, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        if (operations[index] == IL_OP_SWITCH)
            stack_push_i8(&thread, 0);
        else
            stack_push_r8(&thread, 0.0);
        before = *stack_peek(&thread, 0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
        check_fault(EXECUTOR_TYPE_MISMATCH, 0, 1);
        CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
    }
}

TEST_CASE(incompatible_comparison_pairs_are_not_implicitly_promoted)
{
    static const uint8_t kinds[][2] = {
        { STACK_K_I4, STACK_K_I8 }, { STACK_K_I4, STACK_K_R8 },
        { STACK_K_NINT, STACK_K_R8 }, { STACK_K_REF, STACK_K_I4 },
        { STACK_K_BYREF, STACK_K_BYREF }
    };
    static const uint16_t operations[] = { 0xFE00u | IL_OP_FE_CEQ, 0xFE00u | IL_OP_FE_CGT,
                                          0xFE00u | IL_OP_FE_CGT_UN, 0xFE00u | IL_OP_FE_CLT,
                                          0xFE00u | IL_OP_FE_CLT_UN, IL_OP_BEQ_S, IL_OP_BLT_UN };
    size_t pair;
    size_t operation;

    for (pair = 0; pair < sizeof kinds / sizeof kinds[0]; pair++)
        for (operation = 0; operation < sizeof operations / sizeof operations[0]; operation++) {
            uint8_t code[6];
            uint32_t length;
            stack_slot_t before[2];

            if (operations[operation] > 0xFFu) {
                code[0] = IL_OP_PREFIX1;
                code[1] = (uint8_t)operations[operation];
                length = 2;
            } else {
                length = write_branch(code, operations[operation], 0);
            }
            code[length] = IL_OP_RET;
            memset(before, 0, sizeof before);
            before[0].kind = kinds[pair][0];
            before[1].kind = kinds[pair][1];
            start_thread(FRAMES);
            CHECK_EQ(start_method(code, length + 1, 2, STACK_K_EMPTY), EXECUTOR_CONTINUE);
            stack_push(&thread, &before[0]);
            stack_push(&thread, &before[1]);
            CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
            check_fault(EXECUTOR_TYPE_MISMATCH, 0, 2);
            CHECK(memcmp(before, thread.frame->stack, sizeof before) == 0);
        }
}

TEST_CASE(reference_ordering_outside_null_tests_is_rejected)
{
    static const uint16_t operations[] = { 0xFE00u | IL_OP_FE_CGT, 0xFE00u | IL_OP_FE_CGT_UN,
                                          0xFE00u | IL_OP_FE_CLT, 0xFE00u | IL_OP_FE_CLT_UN,
                                          IL_OP_BGT_S, IL_OP_BGT_UN, IL_OP_BLE_S, IL_OP_BGE_UN };
    size_t index;

    for (index = 0; index < sizeof operations / sizeof operations[0]; index++) {
        uint8_t code[6];
        uint32_t length;
        stack_slot_t before[2];

        if (operations[index] > 0xFFu) {
            code[0] = IL_OP_PREFIX1;
            code[1] = (uint8_t)operations[index];
            length = 2;
        } else {
            length = write_branch(code, operations[index], 0);
        }
        code[length] = IL_OP_RET;
        start_thread(FRAMES);
        CHECK_EQ(start_method(code, length + 1, 2, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        stack_push_ref(&thread, OBJECT_ID(OBJECT_SPACE_STATIC, 8));
        stack_push_ref(&thread, OBJECT_ID(OBJECT_SPACE_STATIC, 16));
        memcpy(before, thread.frame->stack, sizeof before);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
        check_fault(EXECUTOR_TYPE_MISMATCH, 0, 2);
        CHECK(memcmp(before, thread.frame->stack, sizeof before) == 0);
    }
}

TEST_CASE(direct_managed_call_adds_arguments_and_resumes_the_caller)
{
        static const uint8_t argument_types[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
        static const uint8_t caller_code[] = { IL_OP_LDC_I4_2, IL_OP_LDC_I4_3,
                                                                                 IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_LDARG_1, IL_OP_ADD, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 2, .return_kind = STACK_K_I4,
                    .argument_count = 2, .argument_types = argument_types }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK(executor.program == &program);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 2);
        CHECK_EQ(executor.offset, 0);
        CHECK(thread.frame == frames + 1);
        CHECK(thread.ip == callee_code);
        CHECK(frames[0].ip == caller_code + 5);
        CHECK_EQ(thread.frame->args[0].u.i4, 2);
        CHECK_EQ(thread.frame->args[1].u.i4, 3);
        CHECK_EQ(executor_run(&executor, 4), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 1);
        CHECK_EQ(executor.offset, 5);
        CHECK(thread.frame == frames && thread.ip == caller_code + 5);
        CHECK_EQ(stack_depth(&thread), 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 5);
        CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 5);
        check_stack();
}

TEST_CASE(managed_call_preserves_values_below_its_arguments)
{
        static const uint8_t argument_types[] = { EXECUTOR_TYPE_INT32 };
        static const uint8_t caller_code[] = { IL_OP_LDC_I4_S, 10, IL_OP_LDC_I4_5,
                                                                                 IL_OP_CALL, 2, 0, IL_OP_ADD, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4,
                    .argument_count = 1, .argument_types = argument_types }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 7), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 15);
        check_stack();
}

TEST_CASE(void_managed_call_returns_without_finishing_the_root)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_LDC_I4_5, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_NOP, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .return_kind = STACK_K_EMPTY }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 2);
        CHECK_EQ(executor.offset, 1);
        CHECK_EQ(executor_run(&executor, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 1);
        CHECK_EQ(executor.offset, 3);
        CHECK_EQ(stack_depth(&thread), 0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 5);
        check_stack();
}

static void start_call_thread(stack_frame_t * call_frames, uint32_t frame_count)
{
        start_thread(0);
        stack_thread_end(&thread);
        memset(call_frames, GUARD, ((size_t)frame_count + 1) * sizeof *call_frames);
        CHECK(stack_thread_init(&fake_oh, &thread, slots, SLOTS, call_frames, frame_count));
}

static void check_call_frame_guard(const stack_frame_t * call_frames, uint32_t frame_count)
{
        const uint8_t * guard = (const uint8_t *)&call_frames[frame_count];
        size_t index;

        for (index = 0; index < sizeof call_frames[frame_count]; index++)
                CHECK_EQ(guard[index], GUARD);
        check_stack();
}

TEST_CASE(repeated_calls_replace_the_saved_continuation_each_time)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_CALL, 2, 0,
                                                                                 IL_OP_ADD, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDC_I4_5, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.offset, 3);
        CHECK_EQ(stack_peek_i4(&thread, 0), 5);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 2);
        CHECK(frames[0].ip == caller_code + 6);
        CHECK_EQ(slots[0].u.i4, 5);
        CHECK_EQ(executor_run(&executor, 4), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 10);
        check_stack();
}

TEST_CASE(nested_calls_restore_each_method_and_saved_ip)
{
        static stack_frame_t call_frames[4];
        static const uint8_t root_code[] = { IL_OP_CALL, 2, 0, IL_OP_LDC_I4_1,
                                                                             IL_OP_ADD, IL_OP_RET };
        static const uint8_t middle_code[] = { IL_OP_CALL, 3, 0, IL_OP_LDC_I4_2,
                                                                                 IL_OP_ADD, IL_OP_RET };
        static const uint8_t leaf_code[] = { IL_OP_LDC_I4_3, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = root_code, .code_size = sizeof root_code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_I4 },
                { .code = middle_code, .code_size = sizeof middle_code, .method_id = 2,
                    .max_stack = 2, .return_kind = STACK_K_I4 },
                { .code = leaf_code, .code_size = sizeof leaf_code, .method_id = 3,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t program = { methods, 4 };

        start_call_thread(call_frames, 3);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 3);
        CHECK(thread.frame == call_frames + 2);
        CHECK(call_frames[0].ip == root_code + 3);
        CHECK(call_frames[1].ip == middle_code + 3);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 2);
        CHECK_EQ(executor.offset, 3);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 1);
        CHECK_EQ(executor.offset, 3);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 6);
        check_call_frame_guard(call_frames, 3);
}

TEST_CASE(recursive_managed_calls_return_without_c_interpreter_recursion)
{
        static stack_frame_t call_frames[5];
        static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32 };
        static const uint8_t code[] = {
                IL_OP_LDARG_0, IL_OP_BRFALSE_S, 9,
                IL_OP_LDARG_0, IL_OP_LDC_I4_1, IL_OP_SUB, IL_OP_CALL, 1, 0,
                IL_OP_LDC_I4_1, IL_OP_ADD, IL_OP_RET, IL_OP_LDC_I4_0, IL_OP_RET
        };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = code, .code_size = sizeof code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_I4,
                    .argument_count = 1, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 2 };
        uint32_t budget_count;

        start_call_thread(call_frames, 4);
        stack_push_i4(&thread, 3);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        for (budget_count = 0; budget_count < 30 && executor.status == EXECUTOR_CONTINUE; budget_count++) {
                executor_run(&executor, 2);
                check_call_frame_guard(call_frames, 4);
        }
        CHECK_EQ(executor.status, EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 3);
        CHECK(thread.frame == NULL && thread.ip == NULL);
        check_call_frame_guard(call_frames, 4);
}

TEST_CASE(callee_arguments_narrow_at_each_managed_call_boundary)
{
        static const uint8_t caller_code[] = { IL_OP_LDC_I4, 0xFF, 0xFF, 0x34, 0x12,
                                                                                 IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_RET };
        static const struct {
                uint8_t type;
                int32_t stored;
                int32_t expected;
        } arguments[] = {
                { EXECUTOR_TYPE_INT8, 255, -1 }, { EXECUTOR_TYPE_UINT8, 255, 255 },
                { EXECUTOR_TYPE_INT16, 65535, -1 }, { EXECUTOR_TYPE_UINT16, 65535, 65535 }
        };
        size_t index;

        for (index = 0; index < sizeof arguments / sizeof arguments[0]; index++) {
                uint8_t types[] = { arguments[index].type };
                executor_method_t methods[] = {
                        { 0 },
                        { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                            .max_stack = 1, .return_kind = STACK_K_I4 },
                        { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                            .max_stack = 1, .return_kind = STACK_K_I4,
                            .argument_count = 1, .argument_types = types }
                };
                executor_program_t program = { methods, 3 };

                start_thread(FRAMES);
                CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
                CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
                CHECK_EQ(thread.frame->args[0].u.i4, arguments[index].stored);
                CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
                CHECK_EQ(executor.offset, 8);
                CHECK_EQ(stack_peek_i4(&thread, 0), arguments[index].expected);
                CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
                CHECK_EQ(stack_pop_i4(&thread), arguments[index].expected);
                check_stack();
        }
}

TEST_CASE(callee_locals_are_typed_zeros_on_entry)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32 };
        static const uint8_t locals[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
        static const uint8_t caller_code[] = { IL_OP_LDC_I4_2, IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDLOC_0, IL_OP_LDARG_0, IL_OP_ADD,
                                                                                 IL_OP_STLOC_1, IL_OP_LDLOC_1, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 2, .return_kind = STACK_K_I4,
                    .argument_count = 1, .argument_types = arguments,
                    .local_count = 2, .local_types = locals }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(thread.frame->locals[0].kind, STACK_K_I4);
        CHECK_EQ(thread.frame->locals[1].kind, STACK_K_I4);
        CHECK_EQ(thread.frame->locals[0].u.i4, 0);
        CHECK_EQ(thread.frame->locals[1].u.i4, 0);
        CHECK_EQ(executor_run(&executor, 7), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 2);
        check_stack();
}

TEST_CASE(managed_float32_parameters_use_single_precision_storage)
{
        static const uint8_t root_arguments[] = { EXECUTOR_TYPE_FLOAT64 };
        static const uint8_t callee_arguments[] = { EXECUTOR_TYPE_FLOAT32 };
        static const uint8_t caller_code[] = { IL_OP_LDARG_0, IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_POP, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY,
                    .argument_count = 1, .argument_types = root_arguments },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY,
                    .argument_count = 1, .argument_types = callee_arguments }
        };
        static const executor_program_t program = { methods, 3 };
        float stored;

        start_thread(FRAMES);
        stack_push_r8(&thread, 16777217.0);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        memcpy(&stored, &thread.frame->args[0].u, sizeof stored);
        CHECK(stored == 16777216.0f);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK(stack_peek_r8(&thread, 0) == 16777216.0);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK(thread.frame->args[0].u.r8 == 16777217.0);
        CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
        CHECK_EQ(stack_depth(&thread), 0);
        check_stack();
}

TEST_CASE(caller_and_callee_reference_slots_remain_visible_as_roots)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_REF };
        static const uint8_t caller_code[] = { IL_OP_LDARG_0, IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_POP, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY,
                    .argument_count = 1, .argument_types = arguments },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY,
                    .argument_count = 1, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 3 };
        object_id_t object;

        start_thread(FRAMES);
        object = objects_create(&fake_oh, FAKE_T_DATA, 4);
        CHECK(object != OBJECT_NULL);
        stack_push_ref(&thread, object);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        scan_roots();
        CHECK_EQ(root_occurrences(object), 2);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(stack_peek_ref(&thread, 0), object);
        scan_roots();
        CHECK_EQ(root_occurrences(object), 3);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        scan_roots();
        CHECK_EQ(root_occurrences(object), 1);
        CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
        scan_roots();
        CHECK_EQ(root_count, 0);
        objects_delete(&fake_oh, object);
        check_stack();
}

TEST_CASE(managed_byref_parameters_still_resolve_the_callers_local_storage)
{
        static const uint8_t root_locals[] = { EXECUTOR_TYPE_INT32 };
        static const uint8_t arguments[] = { EXECUTOR_TYPE_BYREF };
        static const uint8_t caller_code[] = { IL_OP_LDLOCA_S, 0, IL_OP_CALL, 2, 0,
                                                                                 IL_OP_LDLOC_0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_POP, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4,
                    .local_count = 1, .local_types = root_locals },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY,
                    .argument_count = 1, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 3 };
        object_byref_t address;
        int32_t value = 42;

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 2);
        address = stack_peek_byref(&thread, 0);
        CHECK_EQ(OBJECT_SPACE(address.id), OBJECT_SPACE_STACK);
        CHECK_EQ(OBJECT_INDEX(address.id), 0);
        CHECK(stack_byref_ptr(&thread, address) == &frames[0].locals[0].u);
        memcpy(stack_byref_ptr(&thread, address), &value, sizeof value);
        CHECK_EQ(executor_run(&executor, 4), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 42);
        check_stack();
}

TEST_CASE(direct_managed_call_does_not_add_a_virtual_receiver_null_check)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_REF };
        static const uint8_t caller_code[] = { IL_OP_LDARG_0, IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDARG_0, IL_OP_POP, IL_OP_LDC_I4_5, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4,
                    .argument_count = 1, .argument_types = arguments },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4,
                    .argument_count = 1, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        stack_push_ref(&thread, OBJECT_NULL);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 7), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 5);
        check_stack();
}

TEST_CASE(method_ids_use_both_little_endian_operand_bytes)
{
        static executor_method_t methods[258];
        static const uint8_t caller_code[] = { IL_OP_CALL, 1, 1, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDC_I4_5, IL_OP_RET };
        executor_program_t program = { methods, 258 };

        start_thread(FRAMES);
        memset(methods, 0, sizeof methods);
        methods[1].code = caller_code;
        methods[1].code_size = sizeof caller_code;
        methods[1].method_id = 1;
        methods[1].max_stack = 1;
        methods[1].return_kind = STACK_K_I4;
        methods[257].code = callee_code;
        methods[257].code_size = sizeof callee_code;
        methods[257].method_id = 257;
        methods[257].max_stack = 1;
        methods[257].return_kind = STACK_K_I4;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 257);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 5);
        check_stack();
}

TEST_CASE(call_without_a_program_reports_bad_method)
{
        static const uint8_t code[] = { IL_OP_CALL, 1, 0, IL_OP_RET };

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_METHOD);
        check_fault(EXECUTOR_BAD_METHOD, 0, 0);
}

TEST_CASE(call_operands_check_each_truncation_boundary)
{
        static const uint8_t code[] = { IL_OP_CALL, 2, 0 };
        uint32_t length;

        for (length = 1; length < sizeof code; length++) {
                start_thread(FRAMES);
                CHECK_EQ(start_method(code, length, 1, STACK_K_EMPTY), EXECUTOR_CONTINUE);
                stack_push_i4(&thread, 7);
                CHECK_EQ(executor_step(&executor), EXECUTOR_TRUNCATED_INSTRUCTION);
                check_fault(EXECUTOR_TRUNCATED_INSTRUCTION, 0, 1);
                CHECK_EQ(stack_peek_i4(&thread, 0), 7);
        }
}

TEST_CASE(program_entry_validation_preserves_existing_state)
{
        static const uint8_t code[] = { IL_OP_RET };
        executor_method_t methods[] = {
                { 0 }, { .code = code, .code_size = sizeof code, .method_id = 1,
                                 .return_kind = STACK_K_EMPTY }
        };
        executor_program_t program = { methods, 2 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(NULL, &thread, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        CHECK_EQ(executor_start_program(&executor, NULL, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        CHECK_EQ(executor_start_program(&executor, &thread, NULL, 1), EXECUTOR_INVALID_ARGUMENT);
        program.methods = NULL;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        program.methods = methods;
        program.method_count = 0;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        program.method_count = 32769;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        program.method_count = 2;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 0), EXECUTOR_BAD_METHOD);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 2), EXECUTOR_BAD_METHOD);
        methods[1].method_id = 2;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_BAD_METHOD);
        methods[1].method_id = 1;
        methods[1].code = NULL;
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        methods[1].code = code;
        CHECK_EQ(executor.status, EXECUTOR_IDLE);
        CHECK(thread.frame == NULL && thread.ip == NULL && executor.program == NULL);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_INVALID_ARGUMENT);
        CHECK(executor.program == &program && thread.ip == code);
        CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
        check_stack();
}

TEST_CASE(managed_target_ids_cannot_mismatch_or_escape_the_registry)
{
        static const uint8_t target_code[] = { IL_OP_RET };
        static const uint16_t identifiers[] = { 0, 3, UINT16_MAX, 2 };
        size_t index;

        for (index = 0; index < sizeof identifiers / sizeof identifiers[0]; index++) {
                uint8_t caller_code[] = { IL_OP_CALL, (uint8_t)identifiers[index],
                                                                    (uint8_t)(identifiers[index] >> 8), IL_OP_RET };
                executor_method_t methods[] = {
                        { 0 },
                        { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                            .max_stack = 1, .return_kind = STACK_K_EMPTY },
                        { .code = target_code, .code_size = sizeof target_code, .method_id = 3,
                            .return_kind = STACK_K_EMPTY }
                };
                executor_program_t program = { methods, 3 };

                start_thread(FRAMES);
                CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
                stack_push_i4(&thread, 7);
                CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_METHOD);
                check_fault(EXECUTOR_BAD_METHOD, 0, 1);
                CHECK(frames[0].ip == NULL);
                CHECK_EQ(stack_peek_i4(&thread, 0), 7);
        }
}

TEST_CASE(callee_metadata_is_validated_before_frame_or_argument_changes)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t target_code[] = { IL_OP_RET };
        static const uint8_t narrow[] = { EXECUTOR_TYPE_INT8 };
        static const uint8_t invalid[] = { 0 };
        uint32_t variant;

        for (variant = 0; variant < 8; variant++) {
                executor_method_t methods[] = {
                        { 0 },
                        { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                            .max_stack = 1, .return_kind = STACK_K_EMPTY },
                        { .code = target_code, .code_size = sizeof target_code, .method_id = 2,
                            .return_kind = STACK_K_EMPTY, .argument_count = 1, .argument_types = narrow }
                };
                executor_program_t program = { methods, 3 };
                stack_slot_t before;

                switch (variant) {
                case 0: methods[2].code = NULL; break;
                case 1: methods[2].code_size = 0; break;
                case 2: methods[2].return_kind = STACK_K_R8; break;
                case 3: methods[2].argument_types = NULL; break;
                case 4: methods[2].argument_types = invalid; break;
                case 5: methods[2].local_count = 1; break;
                case 6: methods[2].local_count = 1; methods[2].local_types = invalid; break;
                default: memset(&methods[2], 0, sizeof methods[2]); break;
                }
                start_thread(FRAMES);
                CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
                stack_push_i4(&thread, 0x1FF);
                before = *stack_peek(&thread, 0);
                CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_METHOD);
                check_fault(EXECUTOR_BAD_METHOD, 0, 1);
                CHECK(memcmp(stack_peek(&thread, 0), &before, sizeof before) == 0);
                CHECK(frames[0].ip == NULL);
        }
}

TEST_CASE(call_argument_underflow_preserves_the_caller)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .return_kind = STACK_K_EMPTY, .argument_count = 2, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 3 };
        uint32_t depth;

        for (depth = 0; depth < 2; depth++) {
                start_thread(FRAMES);
                CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
                if (depth != 0)
                        stack_push_i4(&thread, 7);
                CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_UNDERFLOW);
                check_fault(EXECUTOR_STACK_UNDERFLOW, 0, depth);
                CHECK(frames[0].ip == NULL);
                if (depth != 0)
                        CHECK_EQ(stack_peek_i4(&thread, 0), 7);
        }
}

TEST_CASE(call_argument_kind_failure_does_not_narrow_earlier_parameters)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_INT8, EXECUTOR_TYPE_INT32 };
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_EMPTY },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .return_kind = STACK_K_EMPTY, .argument_count = 2, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 3 };
        stack_slot_t before[2];

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 0x1FF);
        stack_push_r8(&thread, 3.25);
        memcpy(before, thread.frame->stack, sizeof before);
        CHECK_EQ(executor_step(&executor), EXECUTOR_TYPE_MISMATCH);
        check_fault(EXECUTOR_TYPE_MISMATCH, 0, 2);
        CHECK(memcmp(before, thread.frame->stack, sizeof before) == 0);
        CHECK(frames[0].ip == NULL);
}

TEST_CASE(caller_result_overflow_refuses_a_zero_argument_call)
{
        static const uint8_t caller_code[] = { IL_OP_LDC_I4_7, IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDC_I4_5, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 1, 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 7);
        CHECK(frames[0].ip == NULL);
}

TEST_CASE(call_frame_exhaustion_preserves_arguments_and_saved_ip)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_INT8 };
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .return_kind = STACK_K_EMPTY, .argument_count = 1, .argument_types = arguments }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(1);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 0x1FF);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 0, 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 0x1FF);
        CHECK(frames[0].ip == NULL);
}

TEST_CASE(call_slot_exhaustion_does_not_modify_arguments)
{
        static const uint8_t arguments[] = { EXECUTOR_TYPE_INT8 };
        static const uint8_t locals[] = { EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32,
                                                                         EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32,
                                                                         EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32,
                                                                         EXECUTOR_TYPE_INT32, EXECUTOR_TYPE_INT32 };
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_EMPTY },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .return_kind = STACK_K_EMPTY, .argument_count = 1, .argument_types = arguments,
                    .local_count = 8, .local_types = locals }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        stack_push_i4(&thread, 0x1FF);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 0, 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 0x1FF);
        CHECK(frames[0].ip == NULL);
}

TEST_CASE(recursive_calls_fail_at_the_managed_frame_limit)
{
        static const uint8_t code[] = { IL_OP_CALL, 1, 0, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 }, { .code = code, .code_size = sizeof code, .method_id = 1,
                                 .return_kind = STACK_K_EMPTY }
        };
        static const executor_program_t program = { methods, 2 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK(thread.frame == frames + 1);
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 0, 0);
        CHECK(frames[0].ip == code + 3);
        CHECK(frames[1].ip == NULL);
}

TEST_CASE(recursive_calls_fail_at_the_value_slot_limit)
{
        static stack_frame_t call_frames[17];
        static const uint8_t locals[] = { EXECUTOR_TYPE_INT32 };
        static const uint8_t code[] = { IL_OP_CALL, 1, 0, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 }, { .code = code, .code_size = sizeof code, .method_id = 1,
                                 .return_kind = STACK_K_EMPTY, .local_count = 1, .local_types = locals }
        };
        static const executor_program_t program = { methods, 2 };

        start_call_thread(call_frames, 16);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 20), EXECUTOR_STACK_OVERFLOW);
        CHECK(thread.frame == call_frames + SLOTS - 1);
        CHECK(thread.sp == slots + SLOTS);
        check_fault(EXECUTOR_STACK_OVERFLOW, 0, 0);
        check_call_frame_guard(call_frames, 16);
}

TEST_CASE(bad_callee_returns_preserve_the_callee_and_caller_frames)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_RET };
        uint32_t variant;

        for (variant = 0; variant < 4; variant++) {
                executor_method_t methods[] = {
                        { 0 },
                        { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                            .max_stack = 1, .return_kind = STACK_K_I4 },
                        { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                            .max_stack = 2, .return_kind = variant == 3 ? STACK_K_EMPTY : STACK_K_I4 }
                };
                executor_program_t program = { methods, 3 };
                executor_status_t status = variant == 2 ? EXECUTOR_TYPE_MISMATCH : EXECUTOR_BAD_RETURN;
                uint32_t depth;

                start_thread(FRAMES);
                CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
                CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
                if (variant == 1) {
                        stack_push_i4(&thread, 7);
                        stack_push_i4(&thread, 8);
                } else if (variant == 2) {
                        stack_push_r8(&thread, 3.25);
                } else if (variant == 3) {
                        stack_push_i4(&thread, 7);
                }
                depth = stack_depth(&thread);
                CHECK_EQ(executor_step(&executor), status);
                check_fault(status, 0, depth);
                CHECK_EQ(executor.method.method_id, 2);
                CHECK(thread.frame == frames + 1);
                CHECK(frames[0].ip == caller_code + 3);
        }
}

TEST_CASE(saved_caller_continuations_must_follow_a_call_instruction)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_LDC_I4_5, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDC_I4_5, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 2, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t program = { methods, 3 };
        const uint8_t * invalid_ips[] = { NULL, caller_code + 1, caller_code + 4,
                                                                            caller_code + sizeof caller_code,
                                                                            (const uint8_t *)((uintptr_t)caller_code - 1u) };
        size_t index;

        for (index = 0; index < sizeof invalid_ips / sizeof invalid_ips[0]; index++) {
                start_thread(FRAMES);
                CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
                CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
                frames[0].ip = invalid_ips[index];
                CHECK_EQ(executor_step(&executor), EXECUTOR_INVALID_STATE);
                check_fault(EXECUTOR_INVALID_STATE, 1, 1);
                CHECK(thread.frame == frames + 1);
                CHECK_EQ(stack_peek_i4(&thread, 0), 5);
                CHECK(frames[0].ip == invalid_ips[index]);
        }
}

TEST_CASE(caller_method_ids_are_validated_before_returning)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDC_I4_5, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        frames[0].method = 3;
        CHECK_EQ(executor_step(&executor), EXECUTOR_INVALID_STATE);
        check_fault(EXECUTOR_INVALID_STATE, 1, 1);
        CHECK(thread.frame == frames + 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 5);
}

TEST_CASE(caller_result_capacity_is_rechecked_before_return)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t callee_code[] = { IL_OP_LDC_I4_5, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        frames[0].max_stack = 0;
        CHECK_EQ(executor_step(&executor), EXECUTOR_STACK_OVERFLOW);
        check_fault(EXECUTOR_STACK_OVERFLOW, 1, 1);
        CHECK(thread.frame == frames + 1);
        CHECK_EQ(stack_peek_i4(&thread, 0), 5);
}

TEST_CASE(return_after_a_final_call_resumes_at_end_of_caller_code)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0 };
        static const uint8_t callee_code[] = { IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .return_kind = STACK_K_EMPTY },
                { .code = callee_code, .code_size = sizeof callee_code, .method_id = 2,
                    .return_kind = STACK_K_EMPTY }
        };
        static const executor_program_t program = { methods, 3 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&executor, 2), EXECUTOR_CONTINUE);
        CHECK_EQ(executor.method.method_id, 1);
        CHECK_EQ(executor.offset, sizeof caller_code);
        CHECK_EQ(executor_step(&executor), EXECUTOR_END_OF_CODE);
        check_fault(EXECUTOR_END_OF_CODE, sizeof caller_code, 0);
}

TEST_CASE(branch_destinations_cannot_enter_managed_call_operands)
{
        static const uint8_t code[] = { IL_OP_BR_S, 1, IL_OP_CALL, 2, 0, IL_OP_RET };

        start_thread(FRAMES);
        CHECK_EQ(start_method(code, sizeof code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_BRANCH_TARGET);
        check_fault(EXECUTOR_BAD_BRANCH_TARGET, 0, 0);
}

TEST_CASE(standalone_start_detaches_a_finished_program_registry)
{
        static const uint8_t code[] = { IL_OP_RET };
        static const uint8_t call_code[] = { IL_OP_CALL, 1, 0, IL_OP_RET };
        static const executor_method_t methods[] = {
                { 0 }, { .code = code, .code_size = sizeof code, .method_id = 1,
                                 .return_kind = STACK_K_EMPTY }
        };
        static const executor_program_t program = { methods, 2 };

        start_thread(FRAMES);
        CHECK_EQ(executor_start_program(&executor, &thread, &program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_RETURNED);
        CHECK_EQ(start_method(call_code, sizeof call_code, 0, STACK_K_EMPTY), EXECUTOR_CONTINUE);
        CHECK(executor.program == NULL);
        CHECK_EQ(executor_step(&executor), EXECUTOR_BAD_METHOD);
        check_fault(EXECUTOR_BAD_METHOD, 0, 0);
}

TEST_CASE(program_contexts_do_not_share_active_methods_or_return_ips)
{
        static const uint8_t caller_code[] = { IL_OP_CALL, 2, 0, IL_OP_RET };
        static const uint8_t first_code[] = { IL_OP_LDC_I4_2, IL_OP_RET };
        static const uint8_t second_code[] = { IL_OP_LDC_I4_3, IL_OP_RET };
        static const executor_method_t first_methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = first_code, .code_size = sizeof first_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_method_t second_methods[] = {
                { 0 },
                { .code = caller_code, .code_size = sizeof caller_code, .method_id = 1,
                    .max_stack = 1, .return_kind = STACK_K_I4 },
                { .code = second_code, .code_size = sizeof second_code, .method_id = 2,
                    .max_stack = 1, .return_kind = STACK_K_I4 }
        };
        static const executor_program_t first_program = { first_methods, 3 };
        static const executor_program_t second_program = { second_methods, 3 };
        stack_slot_t other_slots[SLOTS];
        stack_frame_t other_frames[FRAMES];
        stack_thread_t other_thread;
        executor_t other_executor;

        start_thread(FRAMES);
        memset(&other_thread, 0, sizeof other_thread);
        memset(&other_executor, 0, sizeof other_executor);
        CHECK(stack_thread_init(&fake_oh, &other_thread, other_slots, SLOTS, other_frames, FRAMES));
        CHECK_EQ(executor_start_program(&executor, &thread, &first_program, 1), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_start_program(&other_executor, &other_thread, &second_program, 1),
                         EXECUTOR_CONTINUE);
        CHECK_EQ(executor_step(&executor), EXECUTOR_CONTINUE);
        CHECK_EQ(executor_run(&other_executor, 4), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&other_thread), 3);
        CHECK(executor.method.code == first_code && executor.program == &first_program);
        CHECK_EQ(executor_run(&executor, 3), EXECUTOR_RETURNED);
        CHECK_EQ(stack_pop_i4(&thread), 2);
        stack_thread_end(&other_thread);
        check_stack();
}

static const harness_case_t cases[] = {
    HARNESS_CASE(returns_five_using_object_handler_frames),
    HARNESS_CASE(void_return_pops_the_frame),
    HARNESS_CASE(single_step_and_budget_can_resume),
    HARNESS_CASE(compact_constants_cover_minus_one_through_eight),
    HARNESS_CASE(short_constants_are_sign_extended),
    HARNESS_CASE(full_constants_are_little_endian_signed_i4),
    HARNESS_CASE(dup_and_pop_preserve_the_whole_slot),
    HARNESS_CASE(arithmetic_preserves_operand_order_and_wraps),
    HARNESS_CASE(negation_wraps_the_minimum_i4),
    HARNESS_CASE(truncated_literals_fail_before_reading_operands),
    HARNESS_CASE(truncation_preserves_prior_stack_and_ip),
    HARNESS_CASE(unsupported_opcode_is_not_a_nop),
    HARNESS_CASE(falling_off_code_is_not_a_return),
    HARNESS_CASE(stack_underflow_is_checked_for_each_operation),
    HARNESS_CASE(push_and_dup_cannot_exceed_max_stack),
    HARNESS_CASE(frame_and_slot_limits_refuse_start_without_changes),
    HARNESS_CASE(arithmetic_rejects_non_i4_operands_without_consuming_them),
    HARNESS_CASE(return_requires_the_exact_declared_depth),
    HARNESS_CASE(return_rejects_a_wrong_slot_kind),
    HARNESS_CASE(invalid_arguments_and_idle_execution_are_refused),
    HARNESS_CASE(start_does_not_replace_an_active_execution_or_unconsumed_result),
    HARNESS_CASE(ending_the_thread_invalidates_an_active_execution),
    HARNESS_CASE(contexts_execute_independently_on_the_same_object_handler),
    HARNESS_CASE(arguments_and_locals_use_the_real_frame_windows),
    HARNESS_CASE(argument_entry_narrows_and_loads_use_declared_signedness),
    HARNESS_CASE(local_store_narrows_before_a_widening_load),
    HARNESS_CASE(compact_argument_loads_cover_all_four_indices),
    HARNESS_CASE(compact_local_loads_and_stores_cover_all_four_indices),
    HARNESS_CASE(short_variable_forms_preserve_indices_and_instruction_lengths),
    HARNESS_CASE(wide_variable_forms_use_indices_above_255),
    HARNESS_CASE(each_declared_local_starts_as_a_typed_zero),
    HARNESS_CASE(narrow_argument_and_local_stores_preserve_declared_signedness),
    HARNESS_CASE(wider_numeric_locations_preserve_their_values),
    HARNESS_CASE(float32_arguments_are_stored_as_rounded_single_bytes),
    HARNESS_CASE(float32_local_stores_round_then_load_as_r8),
    HARNESS_CASE(float32_argument_stores_round_without_changing_other_arguments),
    HARNESS_CASE(reference_arguments_and_locals_remain_visible_to_root_scanning),
    HARNESS_CASE(byref_arguments_and_locals_preserve_base_and_offset),
    HARNESS_CASE(address_loads_expose_declared_storage_bytes),
    HARNESS_CASE(entry_validates_all_type_metadata_and_argument_kinds_before_mutation),
    HARNESS_CASE(entry_capacity_failure_does_not_narrow_supplied_arguments),
    HARNESS_CASE(every_variable_form_checks_indices_before_touching_storage),
    HARNESS_CASE(variable_indices_at_the_count_or_uint16_limit_are_rejected),
    HARNESS_CASE(variable_operands_check_every_truncation_boundary),
    HARNESS_CASE(variable_loads_and_addresses_honor_max_stack),
    HARNESS_CASE(variable_stores_check_underflow_without_changing_locations),
    HARNESS_CASE(variable_stores_reject_wrong_kinds_without_consuming_operands),
    HARNESS_CASE(variable_loads_reject_wrong_location_kinds),
    HARNESS_CASE(addresses_of_byref_locations_are_outside_the_profile),
    HARNESS_CASE(unsupported_prefixed_opcode_is_not_a_variable_instruction),
    HARNESS_CASE(short_and_long_branches_are_relative_to_the_instruction_end),
    HARNESS_CASE(truth_branches_consume_the_condition_only_after_validation),
    HARNESS_CASE(comparison_opcodes_produce_typed_boolean_results),
    HARNESS_CASE(backward_branch_respects_and_resumes_instruction_budgets),
    HARNESS_CASE(branches_cannot_target_literal_operand_bytes),
    HARNESS_CASE(all_binary_branches_cover_equal_less_and_greater_outcomes),
    HARNESS_CASE(comparisons_support_i8_and_native_integer_pairs),
    HARNESS_CASE(i8_comparisons_do_not_truncate_to_32_bits),
    HARNESS_CASE(mixed_i4_native_comparisons_preserve_32_bit_signedness),
    HARNESS_CASE(nan_comparisons_distinguish_ordered_and_unordered_forms),
    HARNESS_CASE(nan_binary_branches_follow_unordered_semantics_in_both_forms),
    HARNESS_CASE(floating_zero_and_infinity_compare_without_integer_conversion),
    HARNESS_CASE(reference_identity_and_null_comparisons_do_not_resolve_payloads),
    HARNESS_CASE(truth_branches_support_each_zero_and_nonzero_operand_kind),
    HARNESS_CASE(switch_cases_use_the_end_of_the_whole_table_as_their_base),
    HARNESS_CASE(empty_switch_consumes_the_index_and_falls_through),
    HARNESS_CASE(backward_switch_obeys_the_instruction_budget),
    HARNESS_CASE(long_backward_displacements_are_not_narrowed_to_a_signed_byte),
    HARNESS_CASE(loop_with_arguments_and_locals_can_resume_and_return_a_sum),
    HARNESS_CASE(branch_operands_check_every_truncation_boundary),
    HARNESS_CASE(comparison_prefix_truncation_preserves_operands),
    HARNESS_CASE(switch_headers_and_tables_are_bounded_before_operand_reads),
    HARNESS_CASE(out_of_method_branch_targets_do_not_change_state),
    HARNESS_CASE(targets_cannot_enter_prefixes_indices_or_switch_tables),
    HARNESS_CASE(untaken_truth_branches_still_validate_their_destinations),
    HARNESS_CASE(unselected_switch_destinations_are_not_ignored),
    HARNESS_CASE(comparisons_and_binary_branches_check_depth_without_consuming_values),
    HARNESS_CASE(truth_branches_and_switch_check_underflow_and_operand_kinds),
    HARNESS_CASE(incompatible_comparison_pairs_are_not_implicitly_promoted),
    HARNESS_CASE(reference_ordering_outside_null_tests_is_rejected),
    HARNESS_CASE(direct_managed_call_adds_arguments_and_resumes_the_caller),
    HARNESS_CASE(managed_call_preserves_values_below_its_arguments),
    HARNESS_CASE(void_managed_call_returns_without_finishing_the_root),
    HARNESS_CASE(repeated_calls_replace_the_saved_continuation_each_time),
    HARNESS_CASE(nested_calls_restore_each_method_and_saved_ip),
    HARNESS_CASE(recursive_managed_calls_return_without_c_interpreter_recursion),
    HARNESS_CASE(callee_arguments_narrow_at_each_managed_call_boundary),
    HARNESS_CASE(callee_locals_are_typed_zeros_on_entry),
    HARNESS_CASE(managed_float32_parameters_use_single_precision_storage),
    HARNESS_CASE(caller_and_callee_reference_slots_remain_visible_as_roots),
    HARNESS_CASE(managed_byref_parameters_still_resolve_the_callers_local_storage),
    HARNESS_CASE(direct_managed_call_does_not_add_a_virtual_receiver_null_check),
    HARNESS_CASE(method_ids_use_both_little_endian_operand_bytes),
    HARNESS_CASE(call_without_a_program_reports_bad_method),
    HARNESS_CASE(call_operands_check_each_truncation_boundary),
    HARNESS_CASE(program_entry_validation_preserves_existing_state),
    HARNESS_CASE(managed_target_ids_cannot_mismatch_or_escape_the_registry),
    HARNESS_CASE(callee_metadata_is_validated_before_frame_or_argument_changes),
    HARNESS_CASE(call_argument_underflow_preserves_the_caller),
    HARNESS_CASE(call_argument_kind_failure_does_not_narrow_earlier_parameters),
    HARNESS_CASE(caller_result_overflow_refuses_a_zero_argument_call),
    HARNESS_CASE(call_frame_exhaustion_preserves_arguments_and_saved_ip),
    HARNESS_CASE(call_slot_exhaustion_does_not_modify_arguments),
    HARNESS_CASE(recursive_calls_fail_at_the_managed_frame_limit),
    HARNESS_CASE(recursive_calls_fail_at_the_value_slot_limit),
    HARNESS_CASE(bad_callee_returns_preserve_the_callee_and_caller_frames),
    HARNESS_CASE(saved_caller_continuations_must_follow_a_call_instruction),
    HARNESS_CASE(caller_method_ids_are_validated_before_returning),
    HARNESS_CASE(caller_result_capacity_is_rechecked_before_return),
    HARNESS_CASE(return_after_a_final_call_resumes_at_end_of_caller_code),
    HARNESS_CASE(branch_destinations_cannot_enter_managed_call_operands),
    HARNESS_CASE(standalone_start_detaches_a_finished_program_registry),
    HARNESS_CASE(program_contexts_do_not_share_active_methods_or_return_ips)
};

HARNESS_MAIN(cases)

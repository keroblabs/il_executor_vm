#include "executor.h"
#include "il_opcodes.h"

typedef enum {
    EXECUTOR_COMPARE_EQUAL,
    EXECUTOR_COMPARE_NOT_EQUAL,
    EXECUTOR_COMPARE_LESS,
    EXECUTOR_COMPARE_LESS_EQUAL,
    EXECUTOR_COMPARE_GREATER,
    EXECUTOR_COMPARE_GREATER_EQUAL
} executor_relation_t;

static executor_status_t executor_fail(executor_t * executor, executor_status_t status)
{
    executor->status = status;
    return status;
}

static int32_t executor_i4(uint32_t bits)
{
    if (bits <= (uint32_t)INT32_MAX)
        return (int32_t)bits;
    return INT32_MIN + (int32_t)(bits - ((uint32_t)INT32_MAX + 1u));
}

static uint32_t executor_read_u32(const uint8_t * bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
           | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static executor_status_t executor_decode(const uint8_t * instruction, uint32_t remaining,
                                         uint16_t * opcode, uint32_t * length)
{
    if (remaining == 0)
        return EXECUTOR_END_OF_CODE;
    *opcode = instruction[0];
    *length = 1;
    if (*opcode == IL_OP_PREFIX1) {
        if (remaining < 2)
            return EXECUTOR_TRUNCATED_INSTRUCTION;
        *opcode = 0xFE00u | instruction[1];
        if (instruction[1] >= IL_OP_FE_CEQ && instruction[1] <= IL_OP_FE_CLT_UN)
            *length = 2;
        else if (instruction[1] >= IL_OP_FE_LDARG && instruction[1] <= IL_OP_FE_STLOC)
            *length = 4;
        else
            return EXECUTOR_UNSUPPORTED_OPCODE;
    } else if ((*opcode >= IL_OP_LDARG_0 && *opcode <= IL_OP_STLOC_3)
               || (*opcode >= IL_OP_LDC_I4_M1 && *opcode <= IL_OP_LDC_I4_8)) {
        *length = 1;
    } else if ((*opcode >= IL_OP_LDARG_S && *opcode <= IL_OP_STLOC_S)
               || *opcode == IL_OP_LDC_I4_S
               || (*opcode >= IL_OP_BR_S && *opcode <= IL_OP_BLT_UN_S)) {
        *length = 2;
    } else if (*opcode == IL_OP_LDC_I4
               || (*opcode >= IL_OP_BR && *opcode <= IL_OP_BLT_UN)) {
        *length = 5;
    } else if (*opcode == IL_OP_CALL) {
        *length = 3;
    } else if (*opcode == IL_OP_SWITCH) {
        uint32_t count;

        if (remaining < 5)
            return EXECUTOR_TRUNCATED_INSTRUCTION;
        count = executor_read_u32(instruction + 1);
        if (count > (remaining - 5u) / 4u)
            return EXECUTOR_TRUNCATED_INSTRUCTION;
        *length = 5u + count * 4u;
    } else {
        switch (*opcode) {
        case IL_OP_NOP:
        case IL_OP_DUP:
        case IL_OP_POP:
        case IL_OP_ADD:
        case IL_OP_SUB:
        case IL_OP_MUL:
        case IL_OP_NEG:
        case IL_OP_RET:
            break;
        default:
            return EXECUTOR_UNSUPPORTED_OPCODE;
        }
    }
    return remaining < *length ? EXECUTOR_TRUNCATED_INSTRUCTION : EXECUTOR_CONTINUE;
}

static int executor_boundary(const executor_method_t * method, uint32_t target,
                              uint16_t * previous_opcode)
{
    uint32_t offset = 0;
    uint16_t previous = 0;

    if (target > method->code_size)
        return 0;
    while (offset < target) {
        uint16_t opcode;
        uint32_t length;

        if (executor_decode(method->code + offset, method->code_size - offset, &opcode, &length)
            != EXECUTOR_CONTINUE)
            return 0;
        offset += length;
        previous = opcode;
    }
    if (offset != target)
        return 0;
    if (previous_opcode != NULL)
        *previous_opcode = previous;
    return 1;
}

static int executor_target(const executor_t * executor, uint32_t base, int32_t displacement,
                            uint32_t * destination)
{
    int64_t target = (int64_t)base + displacement;

    if (target < 0 || target >= executor->method.code_size
        || !executor_boundary(&executor->method, (uint32_t)target, NULL))
        return 0;
    *destination = (uint32_t)target;
    return 1;
}

static executor_status_t executor_order(executor_t * executor, executor_relation_t relation,
                                        int is_unsigned, int null_test, int32_t * order)
{
    stack_thread_t * thread = executor->thread;
    const stack_slot_t * left;
    const stack_slot_t * right;
    int numeric;
    int reference;

    if (stack_depth(thread) < 2)
        return EXECUTOR_STACK_UNDERFLOW;
    left = thread->sp - 2;
    right = thread->sp - 1;
    numeric = (left->kind == right->kind
               && left->kind >= STACK_K_I4 && left->kind <= STACK_K_NINT)
              || (left->kind == STACK_K_I4 && right->kind == STACK_K_NINT)
              || (left->kind == STACK_K_NINT && right->kind == STACK_K_I4);
    reference = left->kind == STACK_K_REF && right->kind == STACK_K_REF
                && (relation == EXECUTOR_COMPARE_EQUAL || relation == EXECUTOR_COMPARE_NOT_EQUAL
                    || (null_test && (left->u.id == OBJECT_NULL || right->u.id == OBJECT_NULL)));
    if (!numeric && !reference)
        return EXECUTOR_TYPE_MISMATCH;
    *order = value_compare(thread->oh, left, right, is_unsigned ? VALUE_UNSIGNED : 0);
    return EXECUTOR_CONTINUE;
}

static int executor_predicate(int32_t order, executor_relation_t relation, int is_unsigned)
{
    if (order == VALUE_UNORDERED)
        return is_unsigned && relation != EXECUTOR_COMPARE_EQUAL;
    switch (relation) {
    case EXECUTOR_COMPARE_EQUAL: return order == VALUE_EQUAL;
    case EXECUTOR_COMPARE_NOT_EQUAL: return order != VALUE_EQUAL;
    case EXECUTOR_COMPARE_LESS: return order == VALUE_LESS;
    case EXECUTOR_COMPARE_LESS_EQUAL: return order != VALUE_GREATER;
    case EXECUTOR_COMPARE_GREATER: return order == VALUE_GREATER;
    case EXECUTOR_COMPARE_GREATER_EQUAL: return order != VALUE_LESS;
    }
    return 0;
}

static executor_relation_t executor_branch_relation(uint16_t opcode)
{
    switch (opcode) {
    case IL_OP_BEQ_S: return EXECUTOR_COMPARE_EQUAL;
    case IL_OP_BNE_UN_S: return EXECUTOR_COMPARE_NOT_EQUAL;
    case IL_OP_BGE_S:
    case IL_OP_BGE_UN_S: return EXECUTOR_COMPARE_GREATER_EQUAL;
    case IL_OP_BGT_S:
    case IL_OP_BGT_UN_S: return EXECUTOR_COMPARE_GREATER;
    case IL_OP_BLE_S:
    case IL_OP_BLE_UN_S: return EXECUTOR_COMPARE_LESS_EQUAL;
    default: return EXECUTOR_COMPARE_LESS;
    }
}

static executor_status_t executor_branch(executor_t * executor, uint16_t opcode,
                                         const uint8_t * instruction, uint32_t length,
                                         uint32_t * next_offset)
{
    stack_thread_t * thread = executor->thread;
    int32_t displacement;
    uint32_t target;
    uint8_t consumed = 0;
    int taken = 1;

    if (length == 2) {
        displacement = instruction[1];
        if (displacement >= 128)
            displacement -= 256;
    } else {
        displacement = executor_i4(executor_read_u32(instruction + 1));
        opcode -= IL_OP_BR - IL_OP_BR_S;
    }
    if (!executor_target(executor, *next_offset, displacement, &target))
        return EXECUTOR_BAD_BRANCH_TARGET;
    if (opcode == IL_OP_BRFALSE_S || opcode == IL_OP_BRTRUE_S) {
        const stack_slot_t * condition;

        if (stack_depth(thread) == 0)
            return EXECUTOR_STACK_UNDERFLOW;
        condition = thread->sp - 1;
        switch (condition->kind) {
        case STACK_K_I4: taken = condition->u.i4 != 0; break;
        case STACK_K_I8: taken = condition->u.i8 != 0; break;
        case STACK_K_NINT: taken = condition->u.nint != 0; break;
        case STACK_K_REF: taken = condition->u.id != OBJECT_NULL; break;
        case STACK_K_BYREF: taken = condition->u.byref_base != OBJECT_NULL; break;
        default: return EXECUTOR_TYPE_MISMATCH;
        }
        if (opcode == IL_OP_BRFALSE_S)
            taken = !taken;
        consumed = 1;
    } else if (opcode != IL_OP_BR_S) {
        executor_relation_t relation = executor_branch_relation(opcode);
        int is_unsigned = opcode >= IL_OP_BNE_UN_S;
        int32_t order;
        executor_status_t status = executor_order(executor, relation, is_unsigned, 0, &order);

        if (status != EXECUTOR_CONTINUE)
            return status;
        taken = executor_predicate(order, relation, is_unsigned);
        consumed = 2;
    }
    stack_drop(thread, consumed);
    if (taken)
        *next_offset = target;
    return EXECUTOR_CONTINUE;
}

static executor_status_t executor_switch(executor_t * executor, const uint8_t * instruction,
                                         uint32_t * next_offset)
{
    stack_thread_t * thread = executor->thread;
    uint32_t count = executor_read_u32(instruction + 1);
    uint32_t selected;
    uint32_t index;
    uint32_t destination = *next_offset;

    if (stack_depth(thread) == 0)
        return EXECUTOR_STACK_UNDERFLOW;
    if (thread->sp[-1].kind != STACK_K_I4)
        return EXECUTOR_TYPE_MISMATCH;
    selected = (uint32_t)thread->sp[-1].u.i4;
    for (index = 0; index < count; index++) {
        uint32_t target;
        int32_t displacement = executor_i4(executor_read_u32(instruction + 5u + index * 4u));

        if (!executor_target(executor, *next_offset, displacement, &target))
            return EXECUTOR_BAD_BRANCH_TARGET;
        if (index == selected)
            destination = target;
    }
    stack_drop(thread, 1);
    *next_offset = destination;
    return EXECUTOR_CONTINUE;
}

static uint8_t executor_type_kind(uint8_t type)
{
    if (type >= EXECUTOR_TYPE_INT8 && type <= EXECUTOR_TYPE_UINT32)
        return STACK_K_I4;
    if (type == EXECUTOR_TYPE_INT64 || type == EXECUTOR_TYPE_UINT64)
        return STACK_K_I8;
    if (type == EXECUTOR_TYPE_FLOAT32 || type == EXECUTOR_TYPE_FLOAT64)
        return STACK_K_R8;
    if (type == EXECUTOR_TYPE_NATIVE_INT || type == EXECUTOR_TYPE_NATIVE_UINT)
        return STACK_K_NINT;
    if (type == EXECUTOR_TYPE_REF)
        return STACK_K_REF;
    if (type == EXECUTOR_TYPE_BYREF)
        return STACK_K_BYREF;
    return STACK_K_EMPTY;
}

static void executor_store_value(obj_handler_t * oh, stack_slot_t * location,
                                  const stack_slot_t * source, uint8_t type)
{
    stack_slot_t stored;

    if (type == EXECUTOR_TYPE_REF || type == EXECUTOR_TYPE_BYREF) {
        object_id_t id = source->u.id;
        uint16_t offset = source->aux;

        location->kind = executor_type_kind(type);
        location->flags = 0;
        location->aux = type == EXECUTOR_TYPE_BYREF ? offset : 0;
        objects_write_ref(oh, &location->u.id, id);
        memset((uint8_t *)&location->u + sizeof id, 0, sizeof location->u - sizeof id);
        return;
    }

    value_convert(oh, source, type, 0, &stored);
    if (type == EXECUTOR_TYPE_INT8 || type == EXECUTOR_TYPE_UINT8) {
        uint8_t bytes = (uint8_t)stored.u.i4;

        memset(&stored.u, 0, sizeof stored.u);
        memcpy(&stored.u, &bytes, sizeof bytes);
    } else if (type == EXECUTOR_TYPE_INT16 || type == EXECUTOR_TYPE_UINT16) {
        uint16_t bytes = (uint16_t)stored.u.i4;

        memset(&stored.u, 0, sizeof stored.u);
        memcpy(&stored.u, &bytes, sizeof bytes);
    } else if (type == EXECUTOR_TYPE_FLOAT32) {
        float bytes = (float)stored.u.r8;

        memset(&stored.u, 0, sizeof stored.u);
        memcpy(&stored.u, &bytes, sizeof bytes);
    }
    *location = stored;
}

static void executor_load_value(obj_handler_t * oh, const stack_slot_t * location,
                                 uint8_t type, stack_slot_t * value)
{
    if (type == EXECUTOR_TYPE_REF || type == EXECUTOR_TYPE_BYREF) {
        *value = *location;
    } else if (type == EXECUTOR_TYPE_FLOAT32) {
        float stored;

        memcpy(&stored, &location->u, sizeof stored);
        memset(value, 0, sizeof *value);
        value->kind = STACK_K_R8;
        value->u.r8 = (double)stored;
    } else {
        value_convert(oh, location, type, 0, value);
    }
}

static executor_status_t executor_variable(executor_t * executor, uint16_t opcode,
                                           const uint8_t * instruction, uint32_t remaining,
                                           uint32_t * length)
{
    stack_thread_t * thread = executor->thread;
    stack_slot_t * location;
    stack_slot_t value;
    uint32_t index;
    uint16_t count;
    uint8_t type;
    int argument;

    if (opcode >= IL_OP_LDARG_0 && opcode <= IL_OP_LDARG_3) {
        index = opcode - IL_OP_LDARG_0;
        opcode = IL_OP_LDARG_S;
    } else if (opcode >= IL_OP_LDLOC_0 && opcode <= IL_OP_LDLOC_3) {
        index = opcode - IL_OP_LDLOC_0;
        opcode = IL_OP_LDLOC_S;
    } else if (opcode >= IL_OP_STLOC_0 && opcode <= IL_OP_STLOC_3) {
        index = opcode - IL_OP_STLOC_0;
        opcode = IL_OP_STLOC_S;
    } else if (opcode > 0xFFu) {
        *length = 4;
        if (remaining < *length)
            return EXECUTOR_TRUNCATED_INSTRUCTION;
        index = (uint32_t)instruction[2] | ((uint32_t)instruction[3] << 8);
        switch (opcode) {
        case (0xFE00u | IL_OP_FE_LDARG): opcode = IL_OP_LDARG_S; break;
        case (0xFE00u | IL_OP_FE_LDARGA): opcode = IL_OP_LDARGA_S; break;
        case (0xFE00u | IL_OP_FE_STARG): opcode = IL_OP_STARG_S; break;
        case (0xFE00u | IL_OP_FE_LDLOC): opcode = IL_OP_LDLOC_S; break;
        case (0xFE00u | IL_OP_FE_LDLOCA): opcode = IL_OP_LDLOCA_S; break;
        case (0xFE00u | IL_OP_FE_STLOC): opcode = IL_OP_STLOC_S; break;
        default: return EXECUTOR_UNSUPPORTED_OPCODE;
        }
    } else {
        *length = 2;
        if (remaining < *length)
            return EXECUTOR_TRUNCATED_INSTRUCTION;
        index = instruction[1];
    }

    argument = opcode == IL_OP_LDARG_S || opcode == IL_OP_STARG_S || opcode == IL_OP_LDARGA_S;
    count = argument ? executor->method.argument_count : executor->method.local_count;
    if (index >= count)
        return EXECUTOR_BAD_INDEX;
    type = argument ? executor->method.argument_types[index] : executor->method.local_types[index];
    location = argument ? thread->frame->args + index : thread->frame->locals + index;
    if (location->kind != executor_type_kind(type))
        return EXECUTOR_TYPE_MISMATCH;

    if (opcode == IL_OP_STARG_S || opcode == IL_OP_STLOC_S) {
        if (stack_depth(thread) == 0)
            return EXECUTOR_STACK_UNDERFLOW;
        if (thread->sp[-1].kind != executor_type_kind(type))
            return EXECUTOR_TYPE_MISMATCH;
        executor_store_value(thread->oh, location, thread->sp - 1, type);
        stack_drop(thread, 1);
    } else {
        if (stack_room(thread) == 0)
            return EXECUTOR_STACK_OVERFLOW;
        if (opcode == IL_OP_LDARGA_S || opcode == IL_OP_LDLOCA_S) {
            if (type == EXECUTOR_TYPE_BYREF)
                return EXECUTOR_TYPE_MISMATCH;
            stack_push_byref(thread, stack_slot_byref(thread, location));
        } else {
            executor_load_value(thread->oh, location, type, &value);
            stack_push(thread, &value);
        }
    }
    return EXECUTOR_CONTINUE;
}

static int executor_method_valid(const executor_method_t * method)
{
    uint32_t index;

    if (method == NULL || method->code == NULL || method->code_size == 0
        || method->method_id == 0
        || (method->argument_count != 0 && method->argument_types == NULL)
        || (method->local_count != 0 && method->local_types == NULL)
        || (method->return_kind != STACK_K_EMPTY && method->return_kind != STACK_K_I4))
        return 0;
    for (index = 0; index < method->argument_count; index++)
        if (executor_type_kind(method->argument_types[index]) == STACK_K_EMPTY)
            return 0;
    for (index = 0; index < method->local_count; index++)
        if (executor_type_kind(method->local_types[index]) == STACK_K_EMPTY)
            return 0;
    return 1;
}

static const executor_method_t * executor_lookup(const executor_program_t * program,
                                                  uint16_t method_id)
{
    if (program == NULL || program->methods == NULL || program->method_count > 32768u
        || method_id == 0 || method_id >= program->method_count
        || program->methods[method_id].method_id != method_id)
        return NULL;
    return program->methods + method_id;
}

static int executor_arguments_match(const stack_thread_t * thread,
                                     const executor_method_t * method)
{
    const stack_slot_t * arguments = thread->sp - method->argument_count;
    uint32_t index;

    for (index = 0; index < method->argument_count; index++)
        if (arguments[index].kind != executor_type_kind(method->argument_types[index]))
            return 0;
    return 1;
}

static stack_frame_t * executor_enter_frame(stack_thread_t * thread,
                                            const executor_method_t * method)
{
    stack_frame_t * frame;
    uint32_t index;

    frame = stack_frame_push(thread, method->method_id, method->argument_count,
                              method->local_count, method->max_stack, NULL);
    if (frame == NULL)
        return NULL;
    for (index = 0; index < method->argument_count; index++)
        executor_store_value(thread->oh, frame->args + index, frame->args + index,
                              method->argument_types[index]);
    for (index = 0; index < method->local_count; index++)
        frame->locals[index].kind = executor_type_kind(method->local_types[index]);
    return frame;
}

executor_status_t executor_start(executor_t * executor, stack_thread_t * thread,
                                 const executor_method_t * method)
{
    stack_frame_t * frame;

    if (executor == NULL || thread == NULL || method == NULL
        || thread->oh == NULL || !thread->oh->ready || thread->stack_base == NULL
        || thread->frame != NULL || stack_depth(thread) != method->argument_count)
        return EXECUTOR_INVALID_ARGUMENT;
    if (executor->thread != NULL && executor->frame != NULL
        && executor->thread->frame == executor->frame)
        return EXECUTOR_INVALID_ARGUMENT;
    if (!executor_method_valid(method))
        return EXECUTOR_INVALID_ARGUMENT;
    if (!executor_arguments_match(thread, method))
        return EXECUTOR_TYPE_MISMATCH;
    frame = executor_enter_frame(thread, method);
    if (frame == NULL)
        return EXECUTOR_STACK_OVERFLOW;

    executor->thread = thread;
    executor->frame = frame;
    executor->method = *method;
    executor->offset = 0;
    executor->status = EXECUTOR_CONTINUE;
    executor->program = NULL;
    thread->ip = method->code;
    return EXECUTOR_CONTINUE;
}

executor_status_t executor_start_program(executor_t * executor, stack_thread_t * thread,
                                         const executor_program_t * program, uint16_t entry_id)
{
    const executor_method_t * entry;
    executor_status_t status;

    if (executor == NULL || thread == NULL || program == NULL || program->methods == NULL
        || program->method_count == 0 || program->method_count > 32768u)
        return EXECUTOR_INVALID_ARGUMENT;
    entry = executor_lookup(program, entry_id);
    if (entry == NULL)
        return EXECUTOR_BAD_METHOD;
    status = executor_start(executor, thread, entry);
    if (status == EXECUTOR_CONTINUE)
        executor->program = program;
    return status;
}

static executor_status_t executor_call(executor_t * executor, uint16_t method_id,
                                       uint32_t resume_offset)
{
    stack_thread_t * thread = executor->thread;
    stack_frame_t * caller = thread->frame;
    const executor_method_t * target = executor_lookup(executor->program, method_id);
    stack_frame_t * callee;
    uint32_t remaining_depth;
    uint8_t return_slots;

    if (!executor_method_valid(target))
        return EXECUTOR_BAD_METHOD;
    if (stack_depth(thread) < target->argument_count)
        return EXECUTOR_STACK_UNDERFLOW;
    if (!executor_arguments_match(thread, target))
        return EXECUTOR_TYPE_MISMATCH;
    return_slots = target->return_kind == STACK_K_EMPTY ? 0 : 1;
    remaining_depth = stack_depth(thread) - target->argument_count;
    if (remaining_depth + return_slots > caller->max_stack)
        return EXECUTOR_STACK_OVERFLOW;
    callee = executor_enter_frame(thread, target);
    if (callee == NULL)
        return EXECUTOR_STACK_OVERFLOW;
    caller->ip = executor->method.code + resume_offset;
    executor->frame = callee;
    executor->method = *target;
    executor->offset = 0;
    thread->ip = target->code;
    return EXECUTOR_CONTINUE;
}

static executor_status_t executor_return(executor_t * executor)
{
    stack_thread_t * thread = executor->thread;
    uint8_t return_slots = executor->method.return_kind == STACK_K_EMPTY ? 0 : 1;

    if (stack_depth(thread) != return_slots)
        return EXECUTOR_BAD_RETURN;
    if (return_slots != 0 && thread->sp[-1].kind != STACK_K_I4)
        return EXECUTOR_TYPE_MISMATCH;
    if (thread->frame != thread->frame_base) {
        stack_frame_t * caller = thread->frame - 1;
        const executor_method_t * method = executor_lookup(executor->program, caller->method);
        uintptr_t saved_ip = (uintptr_t)caller->ip;
        uintptr_t code_base;
        uint32_t resume_offset;
        uint32_t depth;
        uint16_t previous_opcode;

        if (!executor_method_valid(method) || caller->ip == NULL)
            return EXECUTOR_INVALID_STATE;
        code_base = (uintptr_t)method->code;
        if (saved_ip < code_base || saved_ip - code_base > method->code_size)
            return EXECUTOR_INVALID_STATE;
        resume_offset = (uint32_t)(saved_ip - code_base);
        if (!executor_boundary(method, resume_offset, &previous_opcode)
            || previous_opcode != IL_OP_CALL)
            return EXECUTOR_INVALID_STATE;
        depth = (uint32_t)(thread->frame->args - caller->stack);
        if (depth + return_slots > caller->max_stack)
            return EXECUTOR_STACK_OVERFLOW;
        stack_frame_pop(thread, return_slots);
        executor->frame = caller;
        executor->method = *method;
        executor->offset = resume_offset;
        thread->ip = caller->ip;
        return EXECUTOR_CONTINUE;
    }
    stack_frame_pop(thread, return_slots);
    executor->offset++;
    thread->ip = NULL;
    return EXECUTOR_RETURNED;
}

executor_status_t executor_step(executor_t * executor)
{
    stack_thread_t * thread;
    const uint8_t * instruction;
    uint32_t remaining;
    uint32_t length = 1;
    uint32_t next_offset;
    uint16_t opcode;
    executor_status_t decoded;

    if (executor == NULL || executor->status == EXECUTOR_IDLE)
        return EXECUTOR_INVALID_ARGUMENT;
    if (executor->status != EXECUTOR_CONTINUE)
        return executor->status;

    thread = executor->thread;
    if (thread == NULL || executor->frame == NULL || thread->frame != executor->frame
        || executor->offset > executor->method.code_size
        || thread->ip != executor->method.code + executor->offset)
        return executor_fail(executor, EXECUTOR_INVALID_STATE);
    if (executor->offset == executor->method.code_size)
        return executor_fail(executor, EXECUTOR_END_OF_CODE);

    instruction = thread->ip;
    remaining = executor->method.code_size - executor->offset;
    decoded = executor_decode(instruction, remaining, &opcode, &length);
    if (decoded != EXECUTOR_CONTINUE)
        return executor_fail(executor, decoded);
    next_offset = executor->offset + length;

    switch (opcode) {
    case IL_OP_NOP:
        break;

    case IL_OP_CALL: {
        uint16_t method_id = (uint16_t)((uint16_t)instruction[1]
                                        | ((uint16_t)instruction[2] << 8));
        executor_status_t status = executor_call(executor, method_id, next_offset);

        if (status != EXECUTOR_CONTINUE)
            return executor_fail(executor, status);
        return EXECUTOR_CONTINUE;
    }

    case IL_OP_BR_S:
    case IL_OP_BRFALSE_S:
    case IL_OP_BRTRUE_S:
    case IL_OP_BEQ_S:
    case IL_OP_BGE_S:
    case IL_OP_BGT_S:
    case IL_OP_BLE_S:
    case IL_OP_BLT_S:
    case IL_OP_BNE_UN_S:
    case IL_OP_BGE_UN_S:
    case IL_OP_BGT_UN_S:
    case IL_OP_BLE_UN_S:
    case IL_OP_BLT_UN_S:
    case IL_OP_BR:
    case IL_OP_BRFALSE:
    case IL_OP_BRTRUE:
    case IL_OP_BEQ:
    case IL_OP_BGE:
    case IL_OP_BGT:
    case IL_OP_BLE:
    case IL_OP_BLT:
    case IL_OP_BNE_UN:
    case IL_OP_BGE_UN:
    case IL_OP_BGT_UN:
    case IL_OP_BLE_UN:
    case IL_OP_BLT_UN: {
        executor_status_t status = executor_branch(executor, opcode, instruction,
                                                    length, &next_offset);

        if (status != EXECUTOR_CONTINUE)
            return executor_fail(executor, status);
        break;
    }

    case IL_OP_SWITCH: {
        executor_status_t status = executor_switch(executor, instruction, &next_offset);

        if (status != EXECUTOR_CONTINUE)
            return executor_fail(executor, status);
        break;
    }

    case (0xFE00u | IL_OP_FE_CEQ):
    case (0xFE00u | IL_OP_FE_CGT):
    case (0xFE00u | IL_OP_FE_CGT_UN):
    case (0xFE00u | IL_OP_FE_CLT):
    case (0xFE00u | IL_OP_FE_CLT_UN): {
        executor_relation_t relation = opcode == (0xFE00u | IL_OP_FE_CEQ)
                                       ? EXECUTOR_COMPARE_EQUAL
                                       : opcode <= (0xFE00u | IL_OP_FE_CGT_UN)
                                         ? EXECUTOR_COMPARE_GREATER : EXECUTOR_COMPARE_LESS;
        int is_unsigned = opcode == (0xFE00u | IL_OP_FE_CGT_UN)
                          || opcode == (0xFE00u | IL_OP_FE_CLT_UN);
        int32_t order;
        executor_status_t status = executor_order(executor, relation, is_unsigned,
                                                    opcode == (0xFE00u | IL_OP_FE_CGT_UN), &order);

        if (status != EXECUTOR_CONTINUE)
            return executor_fail(executor, status);
        stack_drop(thread, 2);
        stack_push_i4(thread, executor_predicate(order, relation, is_unsigned));
        break;
    }

    case IL_OP_LDARG_0:
    case IL_OP_LDARG_1:
    case IL_OP_LDARG_2:
    case IL_OP_LDARG_3:
    case IL_OP_LDLOC_0:
    case IL_OP_LDLOC_1:
    case IL_OP_LDLOC_2:
    case IL_OP_LDLOC_3:
    case IL_OP_STLOC_0:
    case IL_OP_STLOC_1:
    case IL_OP_STLOC_2:
    case IL_OP_STLOC_3:
    case IL_OP_LDARG_S:
    case IL_OP_LDARGA_S:
    case IL_OP_STARG_S:
    case IL_OP_LDLOC_S:
    case IL_OP_LDLOCA_S:
    case IL_OP_STLOC_S:
    case (0xFE00u | IL_OP_FE_LDARG):
    case (0xFE00u | IL_OP_FE_LDARGA):
    case (0xFE00u | IL_OP_FE_STARG):
    case (0xFE00u | IL_OP_FE_LDLOC):
    case (0xFE00u | IL_OP_FE_LDLOCA):
    case (0xFE00u | IL_OP_FE_STLOC): {
        executor_status_t status = executor_variable(executor, opcode, instruction,
                                                      remaining, &length);

        if (status != EXECUTOR_CONTINUE)
            return executor_fail(executor, status);
        break;
    }

    case IL_OP_LDC_I4_M1:
    case IL_OP_LDC_I4_0:
    case IL_OP_LDC_I4_1:
    case IL_OP_LDC_I4_2:
    case IL_OP_LDC_I4_3:
    case IL_OP_LDC_I4_4:
    case IL_OP_LDC_I4_5:
    case IL_OP_LDC_I4_6:
    case IL_OP_LDC_I4_7:
    case IL_OP_LDC_I4_8:
    case IL_OP_LDC_I4_S:
    case IL_OP_LDC_I4: {
        int32_t value;

        if (opcode == IL_OP_LDC_I4) {
            uint32_t bits;

            length = 5;
            if (remaining < length)
                return executor_fail(executor, EXECUTOR_TRUNCATED_INSTRUCTION);
            bits = (uint32_t)instruction[1] | ((uint32_t)instruction[2] << 8)
                   | ((uint32_t)instruction[3] << 16) | ((uint32_t)instruction[4] << 24);
            value = executor_i4(bits);
        } else if (opcode == IL_OP_LDC_I4_S) {
            length = 2;
            if (remaining < length)
                return executor_fail(executor, EXECUTOR_TRUNCATED_INSTRUCTION);
            value = (int32_t)instruction[1];
            if (value >= 128)
                value -= 256;
        } else {
            value = (int32_t)opcode - IL_OP_LDC_I4_0;
        }
        if (stack_room(thread) == 0)
            return executor_fail(executor, EXECUTOR_STACK_OVERFLOW);
        stack_push_i4(thread, value);
        break;
    }

    case IL_OP_DUP:
        if (stack_depth(thread) == 0)
            return executor_fail(executor, EXECUTOR_STACK_UNDERFLOW);
        if (stack_room(thread) == 0)
            return executor_fail(executor, EXECUTOR_STACK_OVERFLOW);
        stack_dup(thread);
        break;

    case IL_OP_POP:
        if (stack_depth(thread) == 0)
            return executor_fail(executor, EXECUTOR_STACK_UNDERFLOW);
        stack_drop(thread, 1);
        break;

    case IL_OP_ADD:
    case IL_OP_SUB:
    case IL_OP_MUL: {
        uint32_t left;
        uint32_t right;
        uint32_t result;

        if (stack_depth(thread) < 2)
            return executor_fail(executor, EXECUTOR_STACK_UNDERFLOW);
        if (thread->sp[-2].kind != STACK_K_I4 || thread->sp[-1].kind != STACK_K_I4)
            return executor_fail(executor, EXECUTOR_TYPE_MISMATCH);
        left = (uint32_t)thread->sp[-2].u.i4;
        right = (uint32_t)thread->sp[-1].u.i4;
        if (opcode == IL_OP_ADD)
            result = left + right;
        else if (opcode == IL_OP_SUB)
            result = left - right;
        else
            result = left * right;
        stack_drop(thread, 2);
        stack_push_i4(thread, executor_i4(result));
        break;
    }

    case IL_OP_NEG: {
        uint32_t result;

        if (stack_depth(thread) == 0)
            return executor_fail(executor, EXECUTOR_STACK_UNDERFLOW);
        if (thread->sp[-1].kind != STACK_K_I4)
            return executor_fail(executor, EXECUTOR_TYPE_MISMATCH);
        result = 0u - (uint32_t)thread->sp[-1].u.i4;
        stack_drop(thread, 1);
        stack_push_i4(thread, executor_i4(result));
        break;
    }

    case IL_OP_RET: {
        executor_status_t status = executor_return(executor);

        executor->status = status;
        return status;
    }

    default:
        return executor_fail(executor, EXECUTOR_UNSUPPORTED_OPCODE);
    }

    executor->offset = next_offset;
    thread->ip = executor->method.code + executor->offset;
    return EXECUTOR_CONTINUE;
}

executor_status_t executor_run(executor_t * executor, uint32_t instruction_budget)
{
    if (executor == NULL || executor->status == EXECUTOR_IDLE)
        return EXECUTOR_INVALID_ARGUMENT;
    while (instruction_budget != 0 && executor->status == EXECUTOR_CONTINUE) {
        executor_step(executor);
        instruction_budget--;
    }
    return executor->status;
}

#include "stack.h"
#include "gc.h"
#include "objects.h"

static int stack_fault(obj_handler_t * oh, const char * where)
{
    objects_corrupt(oh, where, OBJECT_NULL);
    return 0;
}

/* 0 for a record never initialised or already ended; reported when it names its instance. */
static int stack_live(const stack_thread_t * t, const char * where)
{
    if (t != NULL && t->stack_base != NULL)
        return 1;
    if (t != NULL && t->oh != NULL)
        stack_fault(t->oh, where);
    return 0;
}

int stack_thread_init(obj_handler_t * oh, stack_thread_t * t, stack_slot_t * slots,
                      uint32_t slot_count, stack_frame_t * frames, uint32_t frame_count)
{
    if (oh == NULL || !oh->ready)
        return 0;
    if (t == NULL || slots == NULL || slot_count == 0 || (frames == NULL && frame_count != 0))
        return stack_fault(oh, "stack_thread_init: no record, or no slots");
    if (slot_count > 65536u)
        return stack_fault(oh, "stack_thread_init: more slots than a byref can index");
    if (t->stack_base != NULL)
        return stack_fault(oh, "stack_thread_init: a live thread; end it first");
    memset(t, 0, sizeof *t);
    t->oh = oh;
    t->stack_base = slots;
    t->sp = slots;
    t->stack_limit = slots + slot_count;
    if (frame_count != 0) {
        t->frame_base = frames;
        t->frame_limit = frames + frame_count;
    }
    t->next = oh->threads;
    oh->threads = t;
    gc_register_root_scan(oh, stack_scan_roots);
    return 1;
}

void stack_thread_end(stack_thread_t * t)
{
    stack_thread_t ** link;
    obj_handler_t * oh;

    if (!stack_live(t, "stack_thread_end: not a live thread"))
        return;
    oh = t->oh;
    for (link = &oh->threads; *link != NULL; link = &(*link)->next)
        if (*link == t) {
            *link = t->next;
            memset(t, 0, sizeof *t);
            t->oh = oh;                 /* so that a later misuse can still be reported */
            return;
        }
    stack_fault(oh, "stack_thread_end: not on its instance's ring");
}

stack_frame_t * stack_frame_push(stack_thread_t * t, uint16_t method, uint16_t arg_slots,
                                 uint16_t local_slots, uint16_t max_stack,
                                 const uint8_t * local_kinds)
{
    stack_frame_t * f;
    uint32_t i;

    if (!stack_live(t, "stack_frame_push: not a live thread"))
        return NULL;
    if (stack_depth(t) < arg_slots) {
        stack_fault(t->oh, "stack_frame_push: fewer slots than arguments");
        return NULL;
    }
    f = t->frame != NULL ? t->frame + 1 : t->frame_base;
    if (f == t->frame_limit
        || (uint32_t)(t->stack_limit - t->sp) < (uint32_t)local_slots + max_stack)
        return NULL;
    f->args = t->sp - arg_slots;
    f->locals = t->sp;
    f->stack = t->sp + local_slots;
    f->ip = NULL;
    f->method = method;
    f->max_stack = max_stack;
    memset(f->locals, 0, (size_t)local_slots * sizeof *f->locals);
    for (i = 0; local_kinds != NULL && i < local_slots; i++) {
#if STACK_DEBUG
        if (local_kinds[i] > STACK_K_BYREF) {
            stack_fault(t->oh, "stack_frame_push: an unknown local kind");
            continue;
        }
#endif
        f->locals[i].kind = local_kinds[i];
    }
    t->sp = f->stack;
    t->frame = f;
    return f;
}

void stack_frame_pop(stack_thread_t * t, uint8_t ret_slots)
{
    stack_frame_t * f;

    if (!stack_live(t, "stack_frame_pop: not a live thread"))
        return;
    f = t->frame;
    if (f == NULL || ret_slots > 1 || stack_depth(t) < ret_slots) {
        stack_fault(t->oh, "stack_frame_pop: no frame, or no return value");
        return;
    }
    if (ret_slots != 0)
        f->args[0] = t->sp[-1];
    t->sp = f->args + ret_slots;
    t->frame = f != t->frame_base ? f - 1 : NULL;
}

void stack_scan_roots(obj_handler_t * oh, gc_visit_fn visit)
{
    const stack_thread_t * t;
    const stack_slot_t * s;

    for (t = oh->threads; t != NULL; t = t->next)
        for (s = t->stack_base; s < t->sp; s++) {
            object_id_t id = OBJECT_NULL;

            if (s->kind == STACK_K_REF)
                id = s->u.id;
            else if (s->kind == STACK_K_BYREF)
                id = s->u.byref_base;
            if (id != OBJECT_NULL)
                visit(oh, id);
        }
}

/* p lies in [low, stack_limit] on a slot boundary; compared as integers, since p may be wild. */
static int stack_slot_in(const stack_thread_t * t, const stack_slot_t * p, const stack_slot_t * low)
{
    const uintptr_t a = (uintptr_t)p;

    return a >= (uintptr_t)low && a <= (uintptr_t)t->stack_limit
           && (a - (uintptr_t)t->stack_base) % sizeof *p == 0;
}

static int stack_thread_ok(obj_handler_t * oh, const stack_thread_t * t)
{
    const stack_slot_t * low = t->stack_base;
    const stack_frame_t * f;
    const stack_slot_t * s;

    if (t->oh != oh || t->stack_base == NULL
        || (uintptr_t)t->stack_limit < (uintptr_t)t->stack_base)
        return stack_fault(oh, "stack_verify: a thread's record");
    if (t->frame != NULL
        && ((uintptr_t)t->frame < (uintptr_t)t->frame_base
            || (uintptr_t)t->frame >= (uintptr_t)t->frame_limit
            || ((uintptr_t)t->frame - (uintptr_t)t->frame_base) % sizeof *t->frame != 0))
        return stack_fault(oh, "stack_verify: the current frame");
    for (f = t->frame_base; t->frame != NULL && f <= t->frame; f++) {
        if (!stack_slot_in(t, f->args, low) || !stack_slot_in(t, f->locals, f->args)
            || !stack_slot_in(t, f->stack, f->locals)
            || (uint32_t)(t->stack_limit - f->stack) < f->max_stack)
            return stack_fault(oh, "stack_verify: a frame's window");
        low = f->stack;
    }
    if (!stack_slot_in(t, t->sp, low)
        || (t->frame != NULL && (uint32_t)(t->sp - t->frame->stack) > t->frame->max_stack))
        return stack_fault(oh, "stack_verify: sp");
    for (s = t->stack_base; s < t->sp; s++) {
        if (s->kind > STACK_K_BYREF)
            return stack_fault(oh, "stack_verify: a slot's kind");
        if (s->kind == STACK_K_BYREF && OBJECT_SPACE(s->u.byref_base) == OBJECT_SPACE_STACK
            && (OBJECT_INDEX(s->u.byref_base) >= (uint32_t)(t->sp - t->stack_base)
                || s->aux >= sizeof s->u))
            return stack_fault(oh, "stack_verify: a byref past its stack's top");
    }
    return 1;
}

int stack_verify(obj_handler_t * oh)
{
    const stack_thread_t * t;

    for (t = oh->threads; t != NULL; t = t->next)
        if (!stack_thread_ok(oh, t))
            return 0;
    return 1;
}

#if STACK_DEBUG
int stack_check(const stack_thread_t * t, uint64_t depth, uint32_t room, int kind,
                const char * where)
{
    if (!stack_live(t, where))
        return 0;
    if (depth > stack_depth(t) || room > stack_room(t)
        || (kind >= 0 && (t->sp - (size_t)depth)->kind != (uint8_t)kind))
        return stack_fault(t->oh, where);
    return 1;
}

int stack_check_slot(const stack_thread_t * t, uint32_t index, uint32_t offset,
                     const char * where)
{
    if (!stack_live(t, where))
        return 0;
    if (index >= (uint32_t)(t->sp - t->stack_base) || offset >= sizeof t->sp->u
        || t->stack_base[index].kind == STACK_K_EMPTY)
        return stack_fault(t->oh, where);
    return 1;
}
#endif

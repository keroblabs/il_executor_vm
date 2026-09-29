#ifndef STACK_H
#define STACK_H

#include "objects_types.h"
#include "objects.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One call: the callee's window over its thread's value stack (doc 14 section 6.2). */
typedef struct {
    stack_slot_t  * args;           /* this + declared parameters */
    stack_slot_t  * locals;         /* typed zeros from stack_frame_push */
    stack_slot_t  * stack;          /* evaluation stack base */
    const uint8_t * ip;             /* the VM's; never read here */
    uint16_t        method;         /* embedder method id */
    uint16_t        max_stack;      /* evaluation slots reserved */
} stack_frame_t;

/* One managed thread's stacks. The VM owns the record and both arrays (doc 17 section 3). */
typedef struct stack_thread_t {
    obj_handler_t         * oh;
    struct stack_thread_t * next;           /* the instance's ring */
    uint16_t                id;             /* the VM's */
    const uint8_t         * ip;             /* the VM's */
    stack_frame_t         * frame;          /* NULL: no frame pushed */
    stack_slot_t          * stack_base;
    stack_slot_t          * sp;             /* one past the top slot */
    stack_slot_t          * stack_limit;
    stack_frame_t         * frame_base;
    stack_frame_t         * frame_limit;
} stack_thread_t;

/* t must be zeroed, as static storage is, or ended. 1: on the ring; 0: refused. */
int             stack_thread_init(obj_handler_t * oh, stack_thread_t * t,
                                  stack_slot_t * slots, uint32_t slot_count,
                                  stack_frame_t * frames, uint32_t frame_count);
void            stack_thread_end(stack_thread_t * t);

/* NULL: the frame, its locals or its max_stack slots do not fit (StackOverflowException).
   local_kinds: one STACK_K_* per local, each starting as a zero of it; NULL leaves them EMPTY. */
stack_frame_t * stack_frame_push(stack_thread_t * t, uint16_t method, uint16_t arg_slots,
                                 uint16_t local_slots, uint16_t max_stack,
                                 const uint8_t * local_kinds);
/* ret_slots 1 moves the top slot to the callee's args[0], the caller's new top. */
void            stack_frame_pop(stack_thread_t * t, uint8_t ret_slots);

void            stack_scan_roots(obj_handler_t * oh, gc_visit_fn visit);
/* 1: consistent; 0: the first fault went to on_corrupt. */
int             stack_verify(obj_handler_t * oh);

/* Slots on the current frame's evaluation stack; with no frame, on the whole stack. */
static inline uint32_t stack_depth(const stack_thread_t * t)
{
    return (uint32_t)(t->sp - (t->frame != NULL ? t->frame->stack : t->stack_base));
}

/* Slots a push may still fill: the frame's max_stack; with no frame, the rest of the array. */
static inline uint32_t stack_room(const stack_thread_t * t)
{
    return (uint32_t)((t->frame != NULL ? t->frame->stack + t->frame->max_stack
                                        : t->stack_limit) - t->sp);
}

#if STACK_DEBUG
/* Library-internal: the top `depth` slots exist, `room` more fit, the deepest holds `kind`. */
int             stack_check(const stack_thread_t * t, uint64_t depth, uint32_t room, int kind,
                            const char * where);
/* Library-internal: slot `index` is live and typed, and `offset` falls inside its value. */
int             stack_check_slot(const stack_thread_t * t, uint32_t index, uint32_t offset,
                                 const char * where);
#define STACK_CHECK(t, depth, room, kind, where)    stack_check((t), (depth), (room), (kind), (where))
#define STACK_CHECK_SLOT(t, index, offset, where)   stack_check_slot((t), (index), (offset), (where))
#else
#define STACK_CHECK(t, depth, room, kind, where)    1
#define STACK_CHECK_SLOT(t, index, offset, where)   1
#endif

/* ---- push, pop and peek; a peek's index 0 is the top ---------------- */

static inline void stack_push(stack_thread_t * t, const stack_slot_t * s)
{
    if (!STACK_CHECK(t, 0, 1, -1, "stack_push"))
        return;
    *t->sp++ = *s;
}

static inline stack_slot_t stack_pop(stack_thread_t * t)
{
    stack_slot_t none;

    if (!STACK_CHECK(t, 1, 0, -1, "stack_pop")) {
        memset(&none, 0, sizeof none);
        return none;
    }
    return *--t->sp;
}

static inline stack_slot_t * stack_peek(stack_thread_t * t, uint32_t i)
{
    if (!STACK_CHECK(t, (uint64_t)i + 1u, 0, -1, "stack_peek"))
        return NULL;
    return t->sp - 1 - i;
}

#define STACK_KIND_OPS(name, ctype, KIND, member)                                   \
    static inline void stack_push_##name(stack_thread_t * t, ctype v)              \
    {                                                                               \
        stack_slot_t s;                                                             \
                                                                                    \
        memset(&s, 0, sizeof s);                                                    \
        s.kind = KIND;                                                              \
        s.u.member = v;                                                             \
        stack_push(t, &s);                                                          \
    }                                                                               \
    static inline ctype stack_pop_##name(stack_thread_t * t)                       \
    {                                                                               \
        if (!STACK_CHECK(t, 1, 0, KIND, "stack_pop_" #name))                        \
            return 0;                                                               \
        return (--t->sp)->u.member;                                                 \
    }                                                                               \
    static inline ctype stack_peek_##name(const stack_thread_t * t, uint32_t i)    \
    {                                                                               \
        if (!STACK_CHECK(t, (uint64_t)i + 1u, 0, KIND, "stack_peek_" #name))         \
            return 0;                                                               \
        return (t->sp - 1 - i)->u.member;                                           \
    }

STACK_KIND_OPS(i4,   int32_t,     STACK_K_I4,   i4)
STACK_KIND_OPS(i8,   int64_t,     STACK_K_I8,   i8)
STACK_KIND_OPS(r8,   double,      STACK_K_R8,   r8)
STACK_KIND_OPS(nint, int32_t,     STACK_K_NINT, nint)
STACK_KIND_OPS(ref,  object_id_t, STACK_K_REF,  id)

static inline void stack_push_byref(stack_thread_t * t, object_byref_t b)
{
    stack_slot_t s;

    memset(&s, 0, sizeof s);
    s.kind = STACK_K_BYREF;
    s.aux = b.offset;
    s.u.byref_base = b.id;
    stack_push(t, &s);
}

static inline object_byref_t stack_pop_byref(stack_thread_t * t)
{
    object_byref_t b;

    memset(&b, 0, sizeof b);
    if (!STACK_CHECK(t, 1, 0, STACK_K_BYREF, "stack_pop_byref"))
        return b;
    --t->sp;
    b.id = t->sp->u.byref_base;
    b.offset = t->sp->aux;
    return b;
}

static inline object_byref_t stack_peek_byref(const stack_thread_t * t, uint32_t i)
{
    object_byref_t b;

    memset(&b, 0, sizeof b);
    if (!STACK_CHECK(t, (uint64_t)i + 1u, 0, STACK_K_BYREF, "stack_peek_byref"))
        return b;
    b.id = (t->sp - 1 - i)->u.byref_base;
    b.offset = (t->sp - 1 - i)->aux;
    return b;
}

static inline void stack_dup(stack_thread_t * t)
{
    if (!STACK_CHECK(t, 1, 1, -1, "stack_dup"))
        return;
    t->sp[0] = t->sp[-1];
    t->sp++;
}

static inline void stack_drop(stack_thread_t * t, uint32_t n)
{
    if (!STACK_CHECK(t, n, 0, -1, "stack_drop"))
        return;
    t->sp -= n;
}

/* ---- byrefs to locals and arguments (doc 14 section 4.1) ------------ */

/* ldloca and ldarga: a byref that only t can resolve. */
static inline object_byref_t stack_slot_byref(const stack_thread_t * t, const stack_slot_t * slot)
{
    object_byref_t b;
    const uint32_t index = (uint32_t)(slot - t->stack_base);

    memset(&b, 0, sizeof b);
    if (!STACK_CHECK_SLOT(t, index, 0, "stack_slot_byref"))
        return b;
    b.id = OBJECT_ID(OBJECT_SPACE_STACK, index);
    return b;
}

/* Any byref as an address: one into t's stack through t, any other through its space. */
static inline void * stack_byref_ptr(stack_thread_t * t, object_byref_t b)
{
    if (OBJECT_SPACE(b.id) != OBJECT_SPACE_STACK)
        return objects_byref_ptr(t->oh, b);
    if (!STACK_CHECK_SLOT(t, OBJECT_INDEX(b.id), b.offset, "stack_byref_ptr"))
        return NULL;
    return (uint8_t *)&t->stack_base[OBJECT_INDEX(b.id)].u + b.offset;
}

#ifdef __cplusplus
}
#endif

#endif /* STACK_H */

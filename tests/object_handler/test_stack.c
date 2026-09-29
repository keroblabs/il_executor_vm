#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD     4096u
#define HANDLES     128u
#define SLOTS       64u
#define FRAMES      8u

/* One spare element past each array, filled with GUARD, catches a write past it. */
#define GUARD       0x5A

static stack_slot_t   slots_a[SLOTS + 1];
static stack_frame_t  frames_a[FRAMES + 1];
static stack_slot_t   slots_b[SLOTS + 1];
static stack_frame_t  frames_b[FRAMES + 1];
static stack_thread_t ta;
static stack_thread_t tb;

static void start(void)
{
    fake_init(PAYLOAD, HANDLES);
    memset(slots_a, GUARD, sizeof slots_a);
    memset(frames_a, GUARD, sizeof frames_a);
    memset(slots_b, GUARD, sizeof slots_b);
    memset(frames_b, GUARD, sizeof frames_b);
    CHECK(stack_thread_init(&fake_oh, &ta, slots_a, SLOTS, frames_a, FRAMES));
}

static int guard_intact(const void * p, size_t size)
{
    const uint8_t * b = (const uint8_t *)p;
    size_t i;

    for (i = 0; i < size; i++)
        if (b[i] != GUARD)
            return 0;
    return 1;
}

/* ---- a recording visitor, standing in for the collector ------------- */

#define SEEN_MAX    32u

static object_id_t     seen[SEEN_MAX];
static uint32_t        seen_count;
static obj_handler_t * seen_oh;

static void record(obj_handler_t * oh, object_id_t id)
{
    CHECK(seen_count < SEEN_MAX);
    seen_oh = oh;
    seen[seen_count++] = id;
}

static void scan(void)
{
    seen_count = 0;
    CHECK(fake_oh.root_scan == stack_scan_roots);
    fake_oh.root_scan(&fake_oh, record);
}

static uint32_t times_seen(object_id_t id)
{
    uint32_t i;
    uint32_t n = 0;

    for (i = 0; i < seen_count; i++)
        n += seen[i] == id;
    return n;
}

/* ---- threads -------------------------------------------------------- */

TEST_CASE(thread_init_links_the_ring)
{
    start();
    CHECK(fake_oh.threads == &ta);
    CHECK(ta.oh == &fake_oh);
    CHECK(ta.frame == NULL);
    CHECK(ta.sp == slots_a && ta.stack_base == slots_a && ta.stack_limit == slots_a + SLOTS);
    CHECK(ta.frame_base == frames_a && ta.frame_limit == frames_a + FRAMES);
    CHECK_EQ(stack_depth(&ta), 0);
    CHECK_EQ(stack_room(&ta), SLOTS);
    CHECK(fake_oh.root_scan == stack_scan_roots);

    CHECK(stack_thread_init(&fake_oh, &tb, slots_b, SLOTS, frames_b, FRAMES));
    CHECK(fake_oh.threads == &tb && tb.next == &ta && ta.next == NULL);
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(thread_init_refuses_misuse)
{
    obj_handler_t idle;
    stack_thread_t t;

    memset(&idle, 0, sizeof idle);
    memset(&t, 0, sizeof t);
    fake_init(PAYLOAD, HANDLES);
    CHECK(!stack_thread_init(&idle, &t, slots_a, SLOTS, frames_a, FRAMES));
    CHECK(!stack_thread_init(NULL, &t, slots_a, SLOTS, frames_a, FRAMES));
    CHECK_EQ(fake_corrupt_count, 0);            /* no ready instance to report to */

    fake_allow_corrupt();
    CHECK(!stack_thread_init(&fake_oh, NULL, slots_a, SLOTS, frames_a, FRAMES));
    CHECK(!stack_thread_init(&fake_oh, &t, NULL, SLOTS, frames_a, FRAMES));
    CHECK(!stack_thread_init(&fake_oh, &t, slots_a, 0, frames_a, FRAMES));
    CHECK(!stack_thread_init(&fake_oh, &t, slots_a, SLOTS, NULL, FRAMES));
    CHECK_EQ(fake_corrupt_count, 4);
    CHECK(fake_oh.threads == NULL);

    CHECK(stack_thread_init(&fake_oh, &t, slots_a, SLOTS, frames_a, FRAMES));
    CHECK(!stack_thread_init(&fake_oh, &t, slots_b, SLOTS, frames_b, FRAMES));
    CHECK_EQ(fake_corrupt_count, 5);
    CHECK(t.stack_base == slots_a && fake_oh.threads == &t && t.next == NULL);
}

TEST_CASE(frameless_thread_uses_the_whole_array)
{
    stack_thread_t t;
    uint32_t i;

    memset(&t, 0, sizeof t);
    fake_init(PAYLOAD, HANDLES);
    memset(slots_a, GUARD, sizeof slots_a);
    CHECK(stack_thread_init(&fake_oh, &t, slots_a, SLOTS, NULL, 0));
    for (i = 0; i < SLOTS; i++)
        stack_push_i4(&t, (int32_t)i);
    CHECK_EQ(stack_depth(&t), SLOTS);
    CHECK_EQ(stack_room(&t), 0);
    CHECK(stack_frame_push(&t, 1, 0, 0, 0) == NULL);    /* no frames to push */
    CHECK_EQ(stack_peek_i4(&t, SLOTS - 1), 0);
    CHECK_EQ(stack_pop_i4(&t), SLOTS - 1);
    CHECK_EQ(stack_room(&t), 1);
    CHECK(guard_intact(&slots_a[SLOTS], sizeof slots_a[SLOTS]));
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(thread_end_leaves_the_ring)
{
    static stack_slot_t slots_c[1];
    stack_thread_t tc;

    memset(&tc, 0, sizeof tc);
    start();
    CHECK(stack_thread_init(&fake_oh, &tb, slots_b, SLOTS, frames_b, FRAMES));
    CHECK(stack_thread_init(&fake_oh, &tc, slots_c, 1, NULL, 0));
    stack_push_ref(&ta, OBJECT_ID(OBJECT_SPACE_STATIC, 8));
    stack_push_ref(&tb, OBJECT_ID(OBJECT_SPACE_STATIC, 16));

    stack_thread_end(&tb);                      /* the middle of the ring */
    CHECK(fake_oh.threads == &tc && tc.next == &ta && ta.next == NULL);
    CHECK(tb.oh == &fake_oh && tb.stack_base == NULL && tb.sp == NULL && tb.next == NULL);
    scan();
    CHECK_EQ(seen_count, 1);
    CHECK_EQ(times_seen(OBJECT_ID(OBJECT_SPACE_STATIC, 8)), 1);

    fake_allow_corrupt();
    stack_thread_end(&tb);
    CHECK_EQ(fake_corrupt_count, 1);
    CHECK(stack_frame_push(&tb, 1, 0, 0, 1) == NULL);
    stack_frame_pop(&tb, 0);
    CHECK_EQ(fake_corrupt_count, 3);

    CHECK(stack_thread_init(&fake_oh, &tb, slots_b, SLOTS, frames_b, FRAMES));
    CHECK(fake_oh.threads == &tb);
    stack_thread_end(&ta);                      /* the tail */
    stack_thread_end(&tb);                      /* the head */
    CHECK(fake_oh.threads == &tc && tc.next == NULL);
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 3);
}

/* ---- slots ---------------------------------------------------------- */

TEST_CASE(push_pop_and_peek_every_kind)
{
    object_byref_t b;
    object_byref_t got;
    const object_id_t id = OBJECT_ID(OBJECT_SPACE_STATIC, 24);

    start();
    b.id = OBJECT_ID(OBJECT_SPACE_STATIC, 32);
    b.offset = 6;
    b._pad = 0;
    stack_push_i4(&ta, -7);
    stack_push_i8(&ta, INT64_MIN);
    stack_push_r8(&ta, 2.5);
    stack_push_nint(&ta, 0x7FFFFFFF);
    stack_push_ref(&ta, id);
    stack_push_byref(&ta, b);
    CHECK_EQ(stack_depth(&ta), 6);
    CHECK_EQ(stack_room(&ta), SLOTS - 6);

    CHECK_EQ(slots_a[0].kind, STACK_K_I4);
    CHECK_EQ(slots_a[1].kind, STACK_K_I8);
    CHECK_EQ(slots_a[2].kind, STACK_K_R8);
    CHECK_EQ(slots_a[3].kind, STACK_K_NINT);
    CHECK_EQ(slots_a[4].kind, STACK_K_REF);
    CHECK_EQ(slots_a[5].kind, STACK_K_BYREF);
    CHECK_EQ(slots_a[0].flags | slots_a[1].flags | slots_a[4].flags | slots_a[5].flags, 0);
    CHECK_EQ(slots_a[0].aux | slots_a[4].aux, 0);
    CHECK_EQ(slots_a[5].aux, 6);

    got = stack_peek_byref(&ta, 0);
    CHECK(got.id == b.id && got.offset == 6);
    CHECK(stack_peek_ref(&ta, 1) == id);
    CHECK_EQ(stack_peek_nint(&ta, 2), 0x7FFFFFFF);
    CHECK(stack_peek_r8(&ta, 3) == 2.5);
    CHECK(stack_peek_i8(&ta, 4) == INT64_MIN);
    CHECK_EQ(stack_peek_i4(&ta, 5), -7);
    CHECK_EQ(stack_depth(&ta), 6);

    got = stack_pop_byref(&ta);
    CHECK(got.id == b.id && got.offset == 6 && got._pad == 0);
    CHECK(stack_pop_ref(&ta) == id);
    CHECK_EQ(stack_pop_nint(&ta), 0x7FFFFFFF);
    CHECK(stack_pop_r8(&ta) == 2.5);
    CHECK(stack_pop_i8(&ta) == INT64_MIN);
    CHECK_EQ(stack_pop_i4(&ta), -7);
    CHECK_EQ(stack_depth(&ta), 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(whole_slots_dup_and_drop)
{
    stack_slot_t s;
    stack_slot_t * top;

    start();
    memset(&s, 0, sizeof s);
    s.kind = STACK_K_I8;
    s.u.i8 = 42;
    stack_push(&ta, &s);
    stack_dup(&ta);
    CHECK_EQ(stack_depth(&ta), 2);
    CHECK(memcmp(&slots_a[0], &slots_a[1], sizeof s) == 0);

    top = stack_peek(&ta, 0);
    CHECK(top == &slots_a[1]);
    top->u.i8 = 43;                             /* in place, as an opcode does */
    CHECK(stack_peek(&ta, 1) == &slots_a[0]);

    s = stack_pop(&ta);
    CHECK_EQ(s.kind, STACK_K_I8);
    CHECK_EQ(s.u.i8, 43);
    stack_push_i4(&ta, 1);
    stack_push_i4(&ta, 2);
    stack_drop(&ta, 2);
    CHECK_EQ(stack_depth(&ta), 1);
    CHECK_EQ(stack_pop_i8(&ta), 42);
    stack_drop(&ta, 0);
    CHECK_EQ(stack_depth(&ta), 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(debug_checks_bounds_and_kinds)
{
#if STACK_DEBUG
    stack_thread_t t;
    stack_slot_t s;
    uint32_t i;

    memset(&t, 0, sizeof t);
    start();
    fake_allow_corrupt();
    CHECK_EQ(stack_pop_i4(&ta), 0);             /* empty */
    s = stack_pop(&ta);
    CHECK_EQ(s.kind, STACK_K_EMPTY);
    stack_drop(&ta, 1);
    stack_dup(&ta);
    CHECK(ta.sp == slots_a);
    CHECK_EQ(fake_corrupt_count, 4);

    stack_push_r8(&ta, 1.0);
    CHECK_EQ(stack_pop_i4(&ta), 0);             /* the wrong kind */
    CHECK_EQ(stack_peek_ref(&ta, 0), OBJECT_NULL);
    CHECK(stack_peek(&ta, 1) == NULL);
    CHECK(stack_peek(&ta, 0xFFFFFFFFu) == NULL);
    CHECK_EQ(stack_peek_i4(&ta, 0xFFFFFFFFu), 0);
    CHECK_EQ(fake_corrupt_count, 9);
    CHECK_EQ(stack_depth(&ta), 1);
    CHECK(stack_pop_r8(&ta) == 1.0);

    CHECK(stack_frame_push(&ta, 1, 0, 1, 2) != NULL);
    stack_push_i4(&ta, 1);
    stack_push_i4(&ta, 2);
    stack_push_i4(&ta, 3);                      /* past max_stack */
    CHECK_EQ(stack_depth(&ta), 2);
    CHECK_EQ(stack_pop_i4(&ta), 2);
    CHECK_EQ(stack_pop_i4(&ta), 1);
    CHECK_EQ(stack_pop_i4(&ta), 0);             /* the locals are not the frame's to pop */
    CHECK_EQ(fake_corrupt_count, 11);
    stack_frame_pop(&ta, 0);

    CHECK(stack_thread_init(&fake_oh, &t, slots_b, 2, NULL, 0));
    for (i = 0; i < 3; i++)
        stack_push_i4(&t, (int32_t)i);          /* past the array */
    CHECK_EQ(stack_depth(&t), 2);
    CHECK(guard_intact(&slots_b[2], sizeof slots_b[2]));
    CHECK_EQ(fake_corrupt_count, 12);

    stack_thread_end(&t);
    stack_push_i4(&t, 1);
    CHECK_EQ(stack_pop_i4(&t), 0);
    CHECK_EQ(fake_corrupt_count, 14);
    CHECK(stack_verify(&fake_oh));
#else
    harness_skip("per-push checks are a debug-build feature");
#endif
}

/* ---- frames (doc 14 section 6.2) ------------------------------------ */

TEST_CASE(frame_push_overlaps_the_arguments)
{
    stack_frame_t * f;
    stack_frame_t * g;

    start();
    stack_push_i4(&ta, 100);                    /* caller's own value */
    stack_push_i4(&ta, 1);
    stack_push_i4(&ta, 2);
    stack_push_i4(&ta, 3);
    f = stack_frame_push(&ta, 7, 3, 2, 4);
    CHECK(f == &frames_a[0] && ta.frame == f);
    CHECK(f->args == &slots_a[1]);              /* the caller's top three slots */
    CHECK(f->locals == &slots_a[4] && f->stack == &slots_a[6] && ta.sp == f->stack);
    CHECK_EQ(f->method, 7);
    CHECK_EQ(f->max_stack, 4);
    CHECK(f->ip == NULL);
    CHECK_EQ(f->args[0].u.i4, 1);
    CHECK_EQ(f->args[2].u.i4, 3);
    CHECK_EQ(f->locals[0].kind, STACK_K_EMPTY);
    CHECK_EQ(f->locals[1].kind, STACK_K_EMPTY);
    CHECK_EQ(stack_depth(&ta), 0);
    CHECK_EQ(stack_room(&ta), 4);

    stack_push_i4(&ta, 20);
    stack_push_i4(&ta, 21);
    g = stack_frame_push(&ta, 8, 2, 0, 1);
    CHECK(g == &frames_a[1]);
    CHECK(g->args == &f->stack[0] && g->locals == &f->stack[2] && g->stack == g->locals);
    CHECK_EQ(g->args[1].u.i4, 21);
    CHECK_EQ(stack_depth(&ta), 0);
    CHECK_EQ(stack_room(&ta), 1);
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(frame_pop_moves_the_return_value)
{
    stack_frame_t * f;
    stack_slot_t * args;

    start();
    stack_push_i4(&ta, 100);
    stack_push_i4(&ta, 5);
    stack_push_i4(&ta, 6);
    f = stack_frame_push(&ta, 1, 2, 1, 3);
    args = f->args;
    stack_push_i4(&ta, f->args[0].u.i4 + f->args[1].u.i4);
    stack_frame_pop(&ta, 1);
    CHECK(ta.frame == NULL);
    CHECK(ta.sp == args + 1);
    CHECK_EQ(stack_depth(&ta), 2);
    CHECK_EQ(stack_pop_i4(&ta), 11);
    CHECK_EQ(stack_pop_i4(&ta), 100);

    stack_push_i4(&ta, 1);
    f = stack_frame_push(&ta, 1, 0, 0, 1);      /* no arguments: the result lands at the old top */
    CHECK(stack_frame_push(&ta, 2, 0, 0, 0) != NULL);
    stack_frame_pop(&ta, 0);                    /* a void call */
    CHECK(ta.frame == f && ta.sp == f->stack);
    stack_push_ref(&ta, OBJECT_ID(OBJECT_SPACE_STATIC, 8));
    stack_frame_pop(&ta, 1);
    CHECK(ta.frame == NULL);
    CHECK_EQ(stack_depth(&ta), 2);
    CHECK(stack_pop_ref(&ta) == OBJECT_ID(OBJECT_SPACE_STATIC, 8));
    CHECK_EQ(stack_pop_i4(&ta), 1);
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(locals_are_zeroed_so_a_stale_id_is_no_root)
{
    stack_frame_t * f;

    start();
    f = stack_frame_push(&ta, 1, 0, 2, 2);
    f->locals[1].kind = STACK_K_REF;
    f->locals[1].u.id = OBJECT_ID(OBJECT_SPACE_STATIC, 8);
    stack_push_ref(&ta, OBJECT_ID(OBJECT_SPACE_STATIC, 16));
    stack_frame_pop(&ta, 0);
    scan();
    CHECK_EQ(seen_count, 0);                    /* above sp: no longer scanned */

    f = stack_frame_push(&ta, 2, 0, 3, 0);      /* over the same slots */
    CHECK_EQ(f->locals[1].kind, STACK_K_EMPTY);
    CHECK_EQ(f->locals[2].kind, STACK_K_EMPTY);
    scan();
    CHECK_EQ(seen_count, 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(recursion_to_the_frame_limit)
{
    stack_frame_t * f = NULL;
    stack_slot_t * sp;
    uint32_t depth;

    start();
    stack_push_i4(&ta, 0);
    for (depth = 0; depth < FRAMES; depth++) {
        f = stack_frame_push(&ta, 1, 1, 1, 2);
        CHECK(f != NULL);
        stack_push_i4(&ta, (int32_t)depth + 1);  /* the next call's argument */
    }
    sp = ta.sp;
    CHECK(stack_frame_push(&ta, 1, 1, 1, 2) == NULL);
    CHECK(ta.frame == f && ta.sp == sp);
    CHECK(guard_intact(&frames_a[FRAMES], sizeof frames_a[FRAMES]));
    CHECK(guard_intact(&slots_a[SLOTS], sizeof slots_a[SLOTS]));

    stack_drop(&ta, 1);
    for (depth = FRAMES; depth > 0; depth--) {
        CHECK_EQ(f->args[0].u.i4, (int32_t)depth - 1);
        stack_push_i4(&ta, f->args[0].u.i4);
        stack_frame_pop(&ta, 1);
        f = ta.frame;
        if (depth > 1)
            stack_drop(&ta, 1);
    }
    CHECK(ta.frame == NULL);
    CHECK_EQ(stack_depth(&ta), 1);
    CHECK_EQ(stack_pop_i4(&ta), 0);
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(recursion_to_the_slot_limit)
{
    stack_frame_t * top;
    uint32_t calls = 0;
    uint32_t i;

    start();
    /* A call needs its locals and max_stack free above sp: 16 here, 10 of them kept. */
    while (stack_frame_push(&ta, 1, 0, 10, 6) != NULL)
        calls++;
    CHECK_EQ(calls, 5);                         /* 5 x 10 + 6 <= 64 < 6 x 10 + 6 */
    CHECK(ta.sp == slots_a + 50 && ta.frame == &frames_a[4]);
    CHECK_EQ(stack_room(&ta), 6);
    top = ta.frame;
    for (i = 0; i < 6; i++)
        stack_push_i4(&ta, (int32_t)i);         /* the whole of max_stack */
    CHECK(stack_frame_push(&ta, 1, 6, 3, 6) == NULL);     /* 9 needed, 8 left */
    CHECK(ta.frame == top && ta.sp == slots_a + 56);
    CHECK(stack_frame_push(&ta, 1, 6, 2, 6) != NULL);     /* exactly the 8 left */
    CHECK(ta.sp + 6 == ta.stack_limit);
    CHECK_EQ(ta.frame->args[5].u.i4, 5);
    CHECK(guard_intact(&slots_a[SLOTS], sizeof slots_a[SLOTS]));
    CHECK(guard_intact(&frames_a[FRAMES], sizeof frames_a[FRAMES]));
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(frame_misuse_is_corrupt)
{
    stack_frame_t * f;

    start();
    fake_allow_corrupt();
    stack_push_i4(&ta, 1);
    CHECK(stack_frame_push(&ta, 1, 2, 0, 0) == NULL);   /* two arguments, one slot */
    stack_frame_pop(&ta, 0);                    /* no frame */
    CHECK_EQ(fake_corrupt_count, 2);
    f = stack_frame_push(&ta, 1, 1, 0, 1);
    CHECK(f != NULL);
    stack_frame_pop(&ta, 1);                    /* no value to return */
    stack_frame_pop(&ta, 2);
    CHECK_EQ(fake_corrupt_count, 4);
    CHECK(ta.frame == f);
    stack_frame_pop(&ta, 0);
    CHECK(ta.frame == NULL && ta.sp == slots_a);
    CHECK(stack_verify(&fake_oh));
}

/* ---- roots (doc 14 section 7.4) ------------------------------------- */

TEST_CASE(scan_reports_references_and_byref_bases_only)
{
    object_id_t looks_alike;
    object_id_t kept;
    object_id_t base;
    object_byref_t b;
    stack_frame_t * f;

    start();
    looks_alike = objects_create(&fake_oh, FAKE_T_DATA, 16);
    kept = objects_create(&fake_oh, FAKE_T_DATA, 16);
    base = objects_create(&fake_oh, FAKE_T_DATA, 16);
    CHECK(looks_alike != OBJECT_NULL && kept != OBJECT_NULL && base != OBJECT_NULL);

    stack_push_i4(&ta, (int32_t)looks_alike);   /* a live id's bit pattern, as an int32 */
    stack_push_nint(&ta, (int32_t)looks_alike);
    stack_push_i8(&ta, (int64_t)looks_alike);
    stack_push_ref(&ta, OBJECT_NULL);
    f = stack_frame_push(&ta, 1, 1, 2, 3);      /* the null reference becomes an argument */
    f->locals[0].kind = STACK_K_REF;
    f->locals[0].u.id = kept;
    b.id = base;
    b.offset = 4;
    b._pad = 0;
    stack_push_byref(&ta, b);
    slots_a[SLOTS - 1].kind = STACK_K_REF;      /* above sp: never scanned */
    slots_a[SLOTS - 1].u.id = looks_alike;

    scan();
    CHECK(seen_oh == &fake_oh);
    CHECK_EQ(seen_count, 2);
    CHECK_EQ(times_seen(kept), 1);
    CHECK_EQ(times_seen(base), 1);
    CHECK_EQ(times_seen(looks_alike), 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(scan_walks_every_thread)
{
    const object_id_t a = OBJECT_ID(OBJECT_SPACE_STATIC, 8);
    const object_id_t b = OBJECT_ID(OBJECT_SPACE_STATIC, 16);

    start();
    CHECK(stack_thread_init(&fake_oh, &tb, slots_b, SLOTS, frames_b, FRAMES));
    stack_push_ref(&ta, a);
    stack_push_ref(&tb, b);
    stack_push_ref(&tb, a);
    scan();
    CHECK_EQ(seen_count, 3);
    CHECK_EQ(times_seen(a), 2);
    CHECK_EQ(times_seen(b), 1);

    stack_thread_end(&ta);
    scan();
    CHECK_EQ(seen_count, 2);
    CHECK_EQ(times_seen(a), 1);
    CHECK_EQ(fake_corrupt_count, 0);
}

/* ---- threads switch by passing the record (doc 14 section 6.2) ------ */

TEST_CASE(threads_switch_by_passing_the_record)
{
    stack_thread_t * run[2];
    uint32_t step;

    start();
    CHECK(stack_thread_init(&fake_oh, &tb, slots_b, SLOTS, frames_b, FRAMES));
    run[0] = &ta;
    run[1] = &tb;
    for (step = 0; step < 200; step++) {
        stack_thread_t * t = run[step % 2];     /* the scheduler's choice */
        const int32_t mine = step % 2 == 0 ? 1000 : 2000;
        const uint32_t phase = (step / 2) % 4;

        if (phase == 0) {
            stack_push_i4(t, mine);
            CHECK(stack_frame_push(t, (uint16_t)mine, 1, 1, 2) != NULL);
        } else if (phase == 1) {
            stack_push_i4(t, t->frame->args[0].u.i4 + 1);
        } else if (phase == 2) {
            CHECK_EQ(stack_peek_i4(t, 0), mine + 1);
            CHECK_EQ(t->frame->method, (uint16_t)mine);
        } else {
            stack_frame_pop(t, 1);
            CHECK_EQ(stack_pop_i4(t), mine + 1);
            CHECK_EQ(stack_depth(t), 0);
        }
        CHECK(stack_verify(&fake_oh));
    }
    CHECK(ta.sp == slots_a && tb.sp == slots_b);
    CHECK_EQ(fake_corrupt_count, 0);
}

/* ---- stack_verify (doc 18 section 2.1, layer 3) --------------------- */

TEST_CASE(verify_catches_damage)
{
    stack_frame_t * f;
    stack_frame_t * g;
    stack_slot_t * saved_sp;
    stack_slot_t * saved_args;
    uint8_t saved_kind;

    start();
    CHECK(stack_thread_init(&fake_oh, &tb, slots_b, SLOTS, frames_b, FRAMES));
    stack_push_i4(&ta, 1);
    f = stack_frame_push(&ta, 1, 1, 1, 3);
    stack_push_i4(&ta, 2);
    g = stack_frame_push(&ta, 2, 1, 0, 2);
    stack_push_r8(&ta, 3.0);
    CHECK(stack_verify(&fake_oh));
    fake_allow_corrupt();

    saved_kind = slots_a[1].kind;
    slots_a[1].kind = 0x7F;                     /* a live local */
    CHECK(!stack_verify(&fake_oh));
    slots_a[1].kind = saved_kind;

    saved_sp = ta.sp;
    ta.sp = ta.stack_limit + 1;
    CHECK(!stack_verify(&fake_oh));
    ta.sp = (stack_slot_t *)(void *)((uint8_t *)saved_sp + 4);
    CHECK(!stack_verify(&fake_oh));
    slots_a[4].kind = STACK_K_I4;               /* as unchecked release pushes would leave them */
    slots_a[5].kind = STACK_K_I4;
    ta.sp = g->stack + 3;                       /* past the frame's max_stack */
    CHECK(!stack_verify(&fake_oh));
    ta.sp = saved_sp;

    saved_args = g->args;
    g->args = f->locals;                        /* below the caller's evaluation stack */
    CHECK(!stack_verify(&fake_oh));
    g->args = saved_args;

    ta.frame = &frames_a[FRAMES];
    CHECK(!stack_verify(&fake_oh));
    ta.frame = g;

    tb.oh = NULL;
    CHECK(!stack_verify(&fake_oh));
    tb.oh = &fake_oh;

    CHECK_EQ(fake_corrupt_count, 7);
    CHECK(stack_verify(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 7);
}

static const harness_case_t cases[] = {
    HARNESS_CASE(thread_init_links_the_ring),
    HARNESS_CASE(thread_init_refuses_misuse),
    HARNESS_CASE(frameless_thread_uses_the_whole_array),
    HARNESS_CASE(thread_end_leaves_the_ring),
    HARNESS_CASE(push_pop_and_peek_every_kind),
    HARNESS_CASE(whole_slots_dup_and_drop),
    HARNESS_CASE(debug_checks_bounds_and_kinds),
    HARNESS_CASE(frame_push_overlaps_the_arguments),
    HARNESS_CASE(frame_pop_moves_the_return_value),
    HARNESS_CASE(locals_are_zeroed_so_a_stale_id_is_no_root),
    HARNESS_CASE(recursion_to_the_frame_limit),
    HARNESS_CASE(recursion_to_the_slot_limit),
    HARNESS_CASE(frame_misuse_is_corrupt),
    HARNESS_CASE(scan_reports_references_and_byref_bases_only),
    HARNESS_CASE(scan_walks_every_thread),
    HARNESS_CASE(threads_switch_by_passing_the_record),
    HARNESS_CASE(verify_catches_damage),
};

HARNESS_MAIN(cases)

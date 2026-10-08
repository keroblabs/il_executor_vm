# Pool Memory Allocator Documentation

## Overview

The `pool.c` module implements a **fixed-size block memory pool allocator** with integrated queue management. This is a common pattern in embedded systems and performance-critical applications where:

- Dynamic memory allocation (`malloc`/`free`) overhead is unacceptable
- Memory fragmentation must be avoided
- Deterministic allocation times are required
- Elements need to be organized in linked queues

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Pool Buffer                              │
├─────────────────┬─────────────────┬─────────────────┬───────────┤
│   Element 0     │   Element 1     │   Element 2     │    ...    │
├────────┬────────┼────────┬────────┼────────┬────────┼───────────┤
│ p_next │ data[] │ p_next │ data[] │ p_next │ data[] │    ...    │
└────────┴────────┴────────┴────────┴────────┴────────┴───────────┘
     ↑                                    ↑
     │         free_pool chain            │
     └────────────────────────────────────┘
```

### Data Structures

#### `pool_element_t`
```c
typedef struct pool_element_t_
{
    struct pool_element_t_ * p_next;  // Link to next element
    uint8_t data[];                    // Flexible array member (user data)
} pool_element_t;
```

Each pool element contains:
- `p_next`: Pointer to link elements in free list or queues
- `data[]`: User's actual data (flexible array member)

**Special sentinel value**: When `p_next == this_element`, the element is allocated but **not in any queue** (detached state).

#### `queue_data_t`
```c
typedef struct
{
    pool_element_t * queue;  // Head of the queue
} queue_data_t;
```

A simple queue head pointer. Multiple queues can share elements from the same pool.

#### `pool_data_t`
```c
typedef struct
{
    uint32_t element_size;           // Size of user data portion
    pool_element_t * free_pool;      // Head of free list
    pool_element_t * last_free;      // Tail of free list (for O(1) append)
    queue_data_t     active;         // Default "active" queue
} pool_data_t;
```

The main pool control structure containing:
- Element size configuration
- Free list with head and tail pointers
- A built-in default queue called `active`

---

## API Reference

### Macros

#### `POOL_ELEMENT_SIZEOF(t)`
```c
#define POOL_ELEMENT_SIZEOF(t) (sizeof(t) + sizeof(pool_element_t))
```
Calculates total memory needed per element (user struct + management overhead).

#### `DECLARE_POOL(t, c)`
```c
#define DECLARE_POOL(t, c)  static pool_data_t _##t##_pool_data;                           \
                            static uint8_t     _##t##_pool_buffer[POOL_ELEMENT_SIZEOF(t) * c]
```
Declares a pool for type `t` with capacity `c` elements.

**Example:**
```c
DECLARE_POOL(my_struct_t, 10);  // Pool of 10 my_struct_t elements
```

#### `DECLARE_POOL_ATTR(t, c, a)`
Same as `DECLARE_POOL` but adds attribute `a` (e.g., `__attribute__((aligned(4)))`).

#### `CREATE_POOL(t)`
```c
#define CREATE_POOL(t)  pool_create(&_##t##_pool_data, sizeof(t), _##t##_pool_buffer, sizeof(_##t##_pool_buffer));
```
Initializes a previously declared pool at runtime.

#### `GET_POOL_DATA(t)`
```c
#define GET_POOL_DATA(t) &_##t##_pool_data
```
Returns pointer to the pool's control structure.

#### `POOL_CALLBACK(cb)`
```c
#define POOL_CALLBACK(cb) ((pool_search_hdl)cb)
```
Casts a function to the search/compare callback type.

---

### Functions

#### `pool_create`
```c
void pool_create(pool_data_t * pool_data, uint32_t element_size,
                 void * buffer, uint32_t buffer_size);
```

**Purpose:** Initialize a memory pool from a pre-allocated buffer.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `pool_data` | Pool control structure to initialize |
| `element_size` | Size of user data (excluding `pool_element_t` header) |
| `buffer` | Pre-allocated memory buffer |
| `buffer_size` | Total size of buffer in bytes |

**Behavior:**
1. Walks through the buffer, carving out elements
2. Links all elements into the free list
3. Initializes `free_pool` as head, `last_free` as tail
4. Sets `active` queue to empty

**Diagram after initialization:**
```
free_pool → [elem0] → [elem1] → [elem2] → ... → [elemN] → NULL
                                                    ↑
                                                last_free
```

---

#### `pool_malloc_element`
```c
void * pool_malloc_element(pool_data_t * pool_data);
```

**Purpose:** Allocate an element from the pool.

**Returns:** Pointer to user data area, or triggers assert if pool is empty.

**Behavior:**
1. Removes head element from free list
2. Sets `p_next = this_element` (sentinel: allocated but not queued)
3. Returns pointer to `data[]` member

**Time complexity:** O(1)

**Example:**
```c
my_struct_t * obj = (my_struct_t *)pool_malloc_element(GET_POOL_DATA(my_struct_t));
obj->value = 42;
```

---

#### `pool_free_element`
```c
void pool_free_element(pool_data_t * pool_data, void ** p_element);
```

**Purpose:** Return an element to the free pool.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `pool_data` | Pool to return element to |
| `p_element` | Pointer to pointer to element (set to NULL after free) |

**Precondition:** Element must NOT be in any queue (`p_next == this_element`).

**Behavior:**
1. Asserts element is not in a queue
2. Appends element to end of free list (via `last_free`)
3. Sets caller's pointer to NULL

**Time complexity:** O(1)

---

#### `pool_add_to_queue`
```c
void pool_add_to_queue(pool_data_t * pool_data, void * p_element,
                       queue_data_t * queue, pool_search_hdl add_hdl);
```

**Purpose:** Add an element to a queue, optionally with ordering.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `pool_data` | Pool the element belongs to |
| `p_element` | Pointer to element's user data |
| `queue` | Target queue (NULL = use `pool_data->active`) |
| `add_hdl` | Comparison callback for ordered insert (NULL = append to end) |

**Callback signature:**
```c
bool_t compare(void * queue_element, void * new_element);
// Return TRUE to insert new_element BEFORE queue_element
```

**Behavior:**
1. If element is already in a queue, removes it first
2. Walks queue, calling `add_hdl` to find insertion point
3. If `add_hdl` returns TRUE, inserts before current element
4. If no match or NULL callback, appends to end

**Time complexity:** O(n) where n = queue length

---

#### `pool_remove_from_queue`
```c
void pool_remove_from_queue(pool_data_t * pool_data, queue_data_t * queue,
                            void * p_element);
```

**Purpose:** Remove a specific element from a queue.

**Behavior:**
1. If element is not in any queue (sentinel), returns immediately
2. Searches queue for the element
3. Unlinks element and sets `p_next = this_element` (detached state)

**Time complexity:** O(n)

---

#### `pool_peek_head`
```c
void * pool_peek_head(pool_data_t * pool_data, queue_data_t * queue);
```

**Purpose:** Get pointer to first element in queue without removing it.

**Returns:** Pointer to user data of head element, or NULL if queue is empty.

**Time complexity:** O(1)

---

#### `pool_get_from_head`
```c
void * pool_get_from_head(pool_data_t * pool_data, queue_data_t * queue);
```

**Purpose:** Remove and return the first element from a queue.

**Returns:** Pointer to user data, or NULL if queue is empty.

**Behavior:** Calls `pool_peek_head`, then `pool_remove_from_queue`.

**Time complexity:** O(1) for removal since it's the head

---

#### `pool_get_chain_element`
```c
void * pool_get_chain_element(pool_data_t * pool_data, queue_data_t * queue,
                              void * p_element);
```

**Purpose:** Iterate through queue elements.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `p_element` | Current element (NULL to get first element) |

**Returns:** Next element in queue, or NULL if end reached.

**Usage pattern:**
```c
void * elem = NULL;
while ((elem = pool_get_chain_element(pool_data, queue, elem)) != NULL)
{
    my_struct_t * obj = (my_struct_t *)elem;
    // Process obj...
}
```

**Time complexity:** O(1) per call

---

#### `pool_locate_element`
```c
void * pool_locate_element(pool_data_t * pool_data, queue_data_t * queue,
                           void * p_data, pool_search_hdl search_hdl);
```

**Purpose:** Find an element in a queue using a search callback.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| `p_data` | User-defined search key/data |
| `search_hdl` | Callback that returns TRUE when element matches |

**Callback signature:**
```c
bool_t search(void * queue_element, void * search_key);
// Return TRUE if queue_element matches search_key
```

**Returns:** Pointer to matching element's user data, or NULL.

**Time complexity:** O(n)

---

## Element State Machine

```
              pool_malloc_element()
    ┌──────────────────────────────────────┐
    │                                      │
    ▼                                      │
┌────────┐   pool_add_to_queue()     ┌─────┴────┐
│  FREE  │ ─────────────────────────►│ IN QUEUE │
│  LIST  │                           │          │
└────────┘                           └────┬─────┘
    ▲                                     │
    │    pool_free_element()              │ pool_remove_from_queue()
    │    (must be detached first)         │
    │                                     ▼
    │                              ┌──────────────┐
    └──────────────────────────────│   DETACHED   │
                                   │(p_next=self) │
                                   └──────────────┘
```

**States:**
1. **FREE LIST**: Element is available for allocation
2. **IN QUEUE**: Element is linked in one or more queues
3. **DETACHED**: Element is allocated but not in any queue (`p_next == this`)

---

## Usage Example

```c
#include "pool.h"

// Define the data structure
typedef struct
{
    uint32_t id;
    uint32_t value;
    char name[32];
} my_data_t;

// Declare pool with 20 elements
DECLARE_POOL(my_data_t, 20);

void example_usage(void)
{
    // Initialize the pool
    CREATE_POOL(my_data_t);

    pool_data_t * pool = GET_POOL_DATA(my_data_t);

    // Allocate elements
    my_data_t * item1 = (my_data_t *)pool_malloc_element(pool);
    my_data_t * item2 = (my_data_t *)pool_malloc_element(pool);

    item1->id = 1;
    item1->value = 100;

    item2->id = 2;
    item2->value = 200;

    // Add to default active queue
    pool_add_to_queue(pool, item1, NULL, NULL);
    pool_add_to_queue(pool, item2, NULL, NULL);

    // Iterate queue
    void * elem = NULL;
    while ((elem = pool_get_chain_element(pool, NULL, elem)) != NULL)
    {
        my_data_t * data = (my_data_t *)elem;
        printf("ID: %u, Value: %u\n", data->id, data->value);
    }

    // Remove from queue and free
    pool_remove_from_queue(pool, NULL, item1);
    pool_free_element(pool, (void **)&item1);  // item1 is now NULL
}
```

---

## Ordered Queue Example

```c
// Comparison: insert in ascending order by value
bool_t insert_ordered(void * queue_elem, void * new_elem)
{
    my_data_t * q = (my_data_t *)queue_elem;
    my_data_t * n = (my_data_t *)new_elem;
    return n->value < q->value;  // Insert before if new is smaller
}

void ordered_queue_example(void)
{
    pool_data_t * pool = GET_POOL_DATA(my_data_t);

    my_data_t * a = pool_malloc_element(pool);
    my_data_t * b = pool_malloc_element(pool);
    my_data_t * c = pool_malloc_element(pool);

    a->value = 30;
    b->value = 10;
    c->value = 20;

    // Add with ordering callback
    pool_add_to_queue(pool, a, NULL, POOL_CALLBACK(insert_ordered));
    pool_add_to_queue(pool, b, NULL, POOL_CALLBACK(insert_ordered));
    pool_add_to_queue(pool, c, NULL, POOL_CALLBACK(insert_ordered));

    // Queue order is now: b(10) -> c(20) -> a(30)
}
```

---

## Multiple Queues Example

```c
// Declare additional queues
static queue_data_t pending_queue;
static queue_data_t completed_queue;

void multi_queue_example(void)
{
    pool_data_t * pool = GET_POOL_DATA(my_data_t);

    // Initialize queues
    pending_queue.queue = NULL;
    completed_queue.queue = NULL;

    my_data_t * task = pool_malloc_element(pool);
    task->id = 42;

    // Add to pending queue
    pool_add_to_queue(pool, task, &pending_queue, NULL);

    // Move to completed queue (auto-removes from pending)
    pool_add_to_queue(pool, task, &completed_queue, NULL);
}
```

---

## Memory Layout

For a pool declared as `DECLARE_POOL(my_data_t, 3)` where `sizeof(my_data_t) = 16`:

```
Buffer layout (total = 3 * (8 + 16) = 72 bytes on 64-bit):

Offset 0:   ┌──────────────────────────┐
            │ pool_element_t (8 bytes) │  ← p_next pointer
            ├──────────────────────────┤
            │ my_data_t (16 bytes)     │  ← user data
            └──────────────────────────┘
Offset 24:  ┌──────────────────────────┐
            │ pool_element_t (8 bytes) │
            ├──────────────────────────┤
            │ my_data_t (16 bytes)     │
            └──────────────────────────┘
Offset 48:  ┌──────────────────────────┐
            │ pool_element_t (8 bytes) │
            ├──────────────────────────┤
            │ my_data_t (16 bytes)     │
            └──────────────────────────┘
```

---

## Thread Safety

**This implementation is NOT thread-safe.** All pool operations involve non-atomic read-modify-write sequences on shared data structures (linked lists). Using this pool in a multi-threaded environment without proper synchronization will result in **undefined behavior**, including:

- Memory corruption
- Double allocation of the same element
- Lost elements (memory leaks)
- Use-after-free bugs
- Infinite loops in list traversal

### Race Condition Analysis

#### Critical Shared State

| Data | Modified By | Risk |
|------|-------------|------|
| `free_pool` (head pointer) | `pool_malloc_element`, `pool_free_element` | Double allocation, lost free elements |
| `last_free` (tail pointer) | `pool_create`, `pool_free_element` | Corrupted free list tail |
| `queue->queue` (queue head) | `pool_add_to_queue`, `pool_remove_from_queue`, `pool_get_from_head` | Lost queue elements, corrupted ordering |
| `element->p_next` | All operations | List corruption, infinite loops |

#### Specific Race Conditions

**Race 1: Concurrent `pool_malloc_element`**
```
Thread A                          Thread B
────────                          ────────
read free_pool → elem1
                                  read free_pool → elem1 (same!)
free_pool = elem1->p_next
                                  free_pool = elem1->p_next
return elem1
                                  return elem1  ← DOUBLE ALLOCATION!
```

**Race 2: `pool_malloc_element` vs `pool_free_element`**
```
Thread A (malloc)                 Thread B (free)
─────────────────                 ───────────────
read free_pool → elem1
                                  last_free->p_next = elem2
                                  last_free = elem2
free_pool = elem1->p_next
                                  (elem2 now orphaned if it was the only free element)
```

**Race 3: Concurrent `pool_add_to_queue`**
```
Thread A                          Thread B
────────                          ────────
traverse to find insert point
                                  traverse to find insert point
prev->p_next = new_elem_A
                                  prev->p_next = new_elem_B  ← elem_A lost!
```

**Race 4: `pool_get_chain_element` during modification**
```
Thread A (iterating)              Thread B (removing)
────────────────────              ──────────────────
elem = current->p_next
                                  remove current from queue
                                  current->p_next = current (sentinel)
access elem->data                 ← elem may now be freed/reallocated!
```

### Synchronization Strategies

#### Strategy 1: Coarse-Grained Mutex (Simplest)

Protect all pool operations with a single mutex per pool:

```c
#include <pthread.h>

typedef struct {
    pool_data_t pool;
    pthread_mutex_t mutex;
} thread_safe_pool_t;

void ts_pool_init(thread_safe_pool_t * ts_pool, uint32_t elem_size,
                  void * buffer, uint32_t buffer_size)
{
    pthread_mutex_init(&ts_pool->mutex, NULL);
    pool_create(&ts_pool->pool, elem_size, buffer, buffer_size);
}

void * ts_pool_malloc(thread_safe_pool_t * ts_pool)
{
    pthread_mutex_lock(&ts_pool->mutex);
    void * elem = pool_malloc_element(&ts_pool->pool);
    pthread_mutex_unlock(&ts_pool->mutex);
    return elem;
}

void ts_pool_free(thread_safe_pool_t * ts_pool, void ** p_element)
{
    pthread_mutex_lock(&ts_pool->mutex);
    pool_free_element(&ts_pool->pool, p_element);
    pthread_mutex_unlock(&ts_pool->mutex);
}

void ts_pool_add_to_queue(thread_safe_pool_t * ts_pool, void * elem,
                          queue_data_t * queue, pool_search_hdl hdl)
{
    pthread_mutex_lock(&ts_pool->mutex);
    pool_add_to_queue(&ts_pool->pool, elem, queue, hdl);
    pthread_mutex_unlock(&ts_pool->mutex);
}

// ... wrap all other operations similarly
```

**Pros:** Simple, correct, easy to reason about
**Cons:** Serializes all operations, potential bottleneck under high contention

#### Strategy 2: Separate Locks for Free List and Queues

```c
typedef struct {
    pool_data_t pool;
    pthread_mutex_t free_list_mutex;   // Protects free_pool, last_free
    pthread_mutex_t queue_mutex;        // Protects queue operations
} dual_lock_pool_t;

void * dl_pool_malloc(dual_lock_pool_t * pool)
{
    pthread_mutex_lock(&pool->free_list_mutex);
    void * elem = pool_malloc_element(&pool->pool);
    pthread_mutex_unlock(&pool->free_list_mutex);
    return elem;
}

void dl_pool_add_to_queue(dual_lock_pool_t * pool, void * elem,
                          queue_data_t * queue, pool_search_hdl hdl)
{
    pthread_mutex_lock(&pool->queue_mutex);
    pool_add_to_queue(&pool->pool, elem, queue, hdl);
    pthread_mutex_unlock(&pool->queue_mutex);
}
```

**Pros:** Allows concurrent malloc/free and queue operations
**Cons:** More complex, must ensure consistent lock ordering to avoid deadlock

#### Strategy 3: Lock-Free Free List (Advanced)

For high-performance scenarios, the free list can be made lock-free using atomic compare-and-swap:

```c
#include <stdatomic.h>

typedef struct {
    _Atomic(pool_element_t *) free_pool;
    uint32_t element_size;
    // Note: last_free optimization not compatible with lock-free
} lockfree_pool_t;

void * lf_pool_malloc(lockfree_pool_t * pool)
{
    pool_element_t * head;
    pool_element_t * next;

    do {
        head = atomic_load(&pool->free_pool);
        if (head == NULL) {
            return NULL;  // Pool exhausted
        }
        next = head->p_next;
    } while (!atomic_compare_exchange_weak(&pool->free_pool, &head, next));

    head->p_next = head;  // Sentinel
    return head->data;
}

void lf_pool_free(lockfree_pool_t * pool, void * element)
{
    pool_element_t * elem = (pool_element_t *)((char *)element - sizeof(pool_element_t));
    pool_element_t * head;

    do {
        head = atomic_load(&pool->free_pool);
        elem->p_next = head;
    } while (!atomic_compare_exchange_weak(&pool->free_pool, &head, elem));
}
```

**Pros:** No locks, excellent scalability
**Cons:** ABA problem potential (mitigated with tagged pointers), complex debugging, queue operations still need locks

#### Strategy 4: Thread-Local Pools

Each thread owns its own pool, eliminating synchronization:

```c
static __thread pool_data_t * tls_pool = NULL;
static __thread uint8_t tls_pool_buffer[POOL_ELEMENT_SIZEOF(my_data_t) * 10];

void * tls_pool_malloc(void)
{
    if (tls_pool == NULL) {
        tls_pool = malloc(sizeof(pool_data_t));
        pool_create(tls_pool, sizeof(my_data_t), tls_pool_buffer, sizeof(tls_pool_buffer));
    }
    return pool_malloc_element(tls_pool);
}
```

**Pros:** Zero synchronization overhead
**Cons:** Memory not shared between threads, higher total memory usage

### Safe Iteration Pattern

When iterating a queue while other threads may modify it, copy elements or hold lock:

```c
// UNSAFE: Other thread may modify queue during iteration
void * elem = NULL;
while ((elem = pool_get_chain_element(pool, queue, elem)) != NULL) {
    process(elem);  // elem may become invalid!
}

// SAFE: Hold lock during entire iteration
pthread_mutex_lock(&pool_mutex);
void * elem = NULL;
while ((elem = pool_get_chain_element(pool, queue, elem)) != NULL) {
    process(elem);
}
pthread_mutex_unlock(&pool_mutex);

// SAFE ALTERNATIVE: Copy to local array under lock, then process
pthread_mutex_lock(&pool_mutex);
void * elements[MAX_ELEMENTS];
int count = 0;
void * elem = NULL;
while ((elem = pool_get_chain_element(pool, queue, elem)) != NULL && count < MAX_ELEMENTS) {
    elements[count++] = elem;
}
pthread_mutex_unlock(&pool_mutex);

for (int i = 0; i < count; i++) {
    process(elements[i]);  // Safe if elements aren't freed
}
```

### Deadlock Prevention

When using multiple locks or multiple pools:

1. **Always acquire locks in consistent order** (e.g., by pool address)
2. **Never hold a pool lock while calling user callbacks** that might acquire other locks
3. **Use try-lock with backoff** for complex scenarios:

```c
bool_t try_move_element(dual_lock_pool_t * src, dual_lock_pool_t * dst, void * elem)
{
    // Ensure consistent ordering by address
    dual_lock_pool_t * first  = (src < dst) ? src : dst;
    dual_lock_pool_t * second = (src < dst) ? dst : src;

    pthread_mutex_lock(&first->queue_mutex);
    pthread_mutex_lock(&second->queue_mutex);

    pool_remove_from_queue(&src->pool, NULL, elem);
    pool_add_to_queue(&dst->pool, elem, NULL, NULL);

    pthread_mutex_unlock(&second->queue_mutex);
    pthread_mutex_unlock(&first->queue_mutex);
    return TRUE;
}
```

### Recommendations

| Use Case | Recommended Strategy |
|----------|---------------------|
| Single-threaded | No synchronization needed |
| Low contention, simple code | Coarse-grained mutex |
| High malloc/free rate | Lock-free free list + mutex for queues |
| Producer-consumer pattern | Separate locks for free list and queues |
| Per-thread object pools | Thread-local storage |
| Real-time systems | Lock-free or disable interrupts |

### Embedded/RTOS Considerations

For embedded systems without pthreads:

```c
// FreeRTOS example
SemaphoreHandle_t pool_sem;

void * rtos_pool_malloc(pool_data_t * pool)
{
    xSemaphoreTake(pool_sem, portMAX_DELAY);
    void * elem = pool_malloc_element(pool);
    xSemaphoreGive(pool_sem);
    return elem;
}

// Bare-metal with interrupt disable
void * irq_safe_pool_malloc(pool_data_t * pool)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    void * elem = pool_malloc_element(pool);
    __set_PRIMASK(primask);
    return elem;
}
```

---

## Error Handling

- `pool_malloc_element`: Asserts (`DO_ASSERT`) if pool is exhausted
- `pool_free_element`: Asserts if element is still in a queue
- No runtime error returns; relies on assertions for debugging

---

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|-----------------|-------|
| `pool_create` | O(n) | Initializes n elements |
| `pool_malloc_element` | O(1) | Head removal |
| `pool_free_element` | O(1) | Tail append |
| `pool_peek_head` | O(1) | |
| `pool_get_from_head` | O(1) | |
| `pool_add_to_queue` | O(n) | Worst case: append to end |
| `pool_remove_from_queue` | O(n) | Linear search |
| `pool_locate_element` | O(n) | Linear search |
| `pool_get_chain_element` | O(1) | Iterator step |

**Space overhead:** `sizeof(pool_element_t)` per element (typically 4-8 bytes for pointer).

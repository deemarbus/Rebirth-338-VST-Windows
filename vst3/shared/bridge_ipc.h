/* ReBirth bridge v1: fixed-layout, local-session, one-client SPSC transport.
 * No pointers, HANDLEs, bools or native-size integers cross the ABI. */
#ifndef RB_BRIDGE_IPC_H
#define RB_BRIDGE_IPC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
typedef LONG RBAtomic32;
#else
typedef int32_t RBAtomic32; /* Layout-only use in portable tests. */
#endif
#define RB_IPC_NAME L"Local\\ReBirth338Bridge.v1"
#define RB_IPC_MAGIC UINT32_C(0x52423338)
enum { RB_IPC_VERSION = 1, RB_RING_CAPACITY = 128, RB_BLOCK_FRAMES = 64,
       RB_SAMPLE_RATE = 44100, RB_TEMPO_MILLI = 140000, RB_LATENCY = 4096 };
enum RBWorkerState { RB_STARTING = 0, RB_NO_SONG = 1, RB_READY = 2,
                     RB_PLAYING = 3, RB_LOOP_ON = 4, RB_STOPPING = 5,
                     RB_FAILED = 6, RB_STOPPED = 7, RB_REARM_REQUIRED = 8 };
#pragma pack(push, 8)
typedef struct {
    uint32_t client_token, epoch;
    uint64_t stream_sample, engine_frame;
    uint32_t playing, reserved;
} RBRequest;
typedef struct {
    uint32_t client_token, epoch;
    uint64_t stream_sample;
    uint32_t state, served_channels, reserved[2];
    float left[RB_BLOCK_FRAMES], right[RB_BLOCK_FRAMES];
} RBReply;
typedef struct {
    volatile RBAtomic32 magic;
    uint32_t version, bytes, frames, sample_rate, tempo_milli, latency, worker_pid;
    volatile RBAtomic32 worker_state, error, heartbeat, client_token;
    volatile RBAtomic32 request_write, request_read, reply_write, reply_read;
    volatile RBAtomic32 underruns, rejected, accepted, client_status, client_generation;
    uint32_t reserved[11];
    RBRequest requests[RB_RING_CAPACITY];
    RBReply replies[RB_RING_CAPACITY];
} RBShared;
#pragma pack(pop)
#if defined(__cplusplus)
#define RB_ASSERT static_assert
#else
#define RB_ASSERT _Static_assert
#endif
RB_ASSERT(sizeof(RBAtomic32) == 4, "32-bit interlocked fields required");
RB_ASSERT(sizeof(RBRequest) == 32, "request ABI");
RB_ASSERT(sizeof(RBReply) == 544, "reply ABI");
RB_ASSERT(offsetof(RBShared, requests) == 128, "shared header ABI");
RB_ASSERT(offsetof(RBShared, replies) == 4224, "reply ring ABI");
RB_ASSERT(sizeof(RBShared) == 73856, "shared mapping ABI");
#ifdef _WIN32
static inline uint32_t rb_load(volatile RBAtomic32 *field) {
    return (uint32_t)InterlockedCompareExchange(field, 0, 0);
}
static inline void rb_store(volatile RBAtomic32 *field, uint32_t value) {
    InterlockedExchange(field, (LONG)value);
}
static inline int rb_valid_header(RBShared *shared) {
    return rb_load(&shared->magic) == RB_IPC_MAGIC &&
        shared->version == RB_IPC_VERSION && shared->bytes == sizeof(RBShared) &&
        shared->frames == RB_BLOCK_FRAMES && shared->sample_rate == RB_SAMPLE_RATE &&
        shared->tempo_milli == RB_TEMPO_MILLI && shared->latency == RB_LATENCY;
}
/* Called during lifecycle activation, with bounded contention retries even
 * when a host activates processing from its render thread. A generation belongs to
 * the mapping lifetime, not to a loaded plug-in module or DAW process. Never
 * wrap/reuse an old token: after UINT32_MAX claims a new worker is required. */
static inline uint32_t rb_claim_client(RBShared *shared) {
    uint32_t previous, candidate, attempt;
    if (rb_load(&shared->client_token)) return 0;
    for (attempt = 0; attempt < 8; ++attempt) {
        previous = rb_load(&shared->client_generation);
        if (previous == UINT32_MAX) return 0;
        candidate = previous + 1u;
        if ((uint32_t)InterlockedCompareExchange(&shared->client_generation,
                (LONG)candidate, (LONG)previous) == previous) break;
    }
    if (attempt == 8) return 0;
    if (InterlockedCompareExchange(&shared->client_token, (LONG)candidate, 0) != 0)
        return 0;
    return candidate;
}
static inline void rb_release_client(RBShared *shared, uint32_t token) {
    if (token) InterlockedCompareExchange(&shared->client_token, 0, (LONG)token);
}
/* Return 1 success, 0 full/empty, -1 malformed counters. Publishing the index
 * with InterlockedExchange supplies the release barrier; rb_load acquires. */
static inline int rb_push_request(RBShared *s, const RBRequest *request) {
    uint32_t w = rb_load(&s->request_write), r = rb_load(&s->request_read);
    if (w - r > RB_RING_CAPACITY) return -1;
    if (w - r == RB_RING_CAPACITY) return 0;
    memcpy(&s->requests[w % RB_RING_CAPACITY], request, sizeof(*request));
    rb_store(&s->request_write, w + 1u);
    return 1;
}
static inline int rb_pop_request(RBShared *s, RBRequest *request) {
    uint32_t w = rb_load(&s->request_write), r = rb_load(&s->request_read);
    if (w - r > RB_RING_CAPACITY) return -1;
    if (w == r) return 0;
    memcpy(request, &s->requests[r % RB_RING_CAPACITY], sizeof(*request));
    rb_store(&s->request_read, r + 1u);
    return 1;
}
static inline int rb_push_reply(RBShared *s, const RBReply *reply) {
    uint32_t w = rb_load(&s->reply_write), r = rb_load(&s->reply_read);
    if (w - r > RB_RING_CAPACITY) return -1;
    if (w - r == RB_RING_CAPACITY) return 0;
    memcpy(&s->replies[w % RB_RING_CAPACITY], reply, sizeof(*reply));
    rb_store(&s->reply_write, w + 1u);
    return 1;
}
static inline int rb_pop_reply(RBShared *s, RBReply *reply) {
    uint32_t w = rb_load(&s->reply_write), r = rb_load(&s->reply_read);
    if (w - r > RB_RING_CAPACITY) return -1;
    if (w == r) return 0;
    memcpy(reply, &s->replies[r % RB_RING_CAPACITY], sizeof(*reply));
    rb_store(&s->reply_read, r + 1u);
    return 1;
}
#endif
#undef RB_ASSERT
#endif

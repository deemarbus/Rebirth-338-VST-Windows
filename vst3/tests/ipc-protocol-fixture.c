/* Actual shared-header tests only: no ReBirth DLL, named worker mapping or
 * engine call. Build on Linux for ABI layout; build as x86/x64 Windows for
 * the actual Interlocked SPSC implementation and a concurrent round trip. */
#include "../shared/bridge_ipc.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
enum { EXCHANGES = 16384, LEASES_PER_THREAD = 1024, TEST_TIMEOUT_MS = 15000 };
static const uint32_t token = UINT32_C(0x3357f1ab);

static int failure(const char *step) {
    fprintf(stderr, "ipc_fixture_failed=%s\n", step);
    return 1;
}

static void header_initialize(RBShared *s) {
    memset(s, 0, sizeof(*s));
    s->version = RB_IPC_VERSION;
    s->bytes = sizeof(*s);
    s->frames = RB_BLOCK_FRAMES;
    s->sample_rate = RB_SAMPLE_RATE;
    s->tempo_milli = RB_TEMPO_MILLI;
    s->latency = RB_LATENCY;
    rb_store(&s->magic, RB_IPC_MAGIC);
}

static RBRequest make_request(uint32_t sequence) {
    RBRequest request = {0};
    request.client_token = token;
    request.epoch = sequence / 257u + 1u;
    request.stream_sample = UINT64_C(0x100000000) + (uint64_t)sequence * RB_BLOCK_FRAMES;
    request.engine_frame = (uint64_t)sequence * RB_BLOCK_FRAMES;
    request.playing = sequence & 1u;
    return request;
}

static RBReply make_reply(const RBRequest *request, uint32_t sequence) {
    unsigned int sample;
    RBReply reply = {0};
    reply.client_token = request->client_token;
    reply.epoch = request->epoch;
    reply.stream_sample = request->stream_sample;
    reply.state = RB_PLAYING;
    reply.served_channels = 2;
    for (sample = 0; sample < RB_BLOCK_FRAMES; ++sample) {
        reply.left[sample] = (float)(sequence * RB_BLOCK_FRAMES + sample);
        reply.right[sample] = -reply.left[sample] - 0.5f;
    }
    return reply;
}

static int serial_checks(RBShared *s) {
    RBRequest request, observed_request, unchanged_request;
    RBReply reply, observed_reply, unchanged_reply;
    uint32_t i, start = UINT32_MAX - 63u;
    header_initialize(s);
    if (!rb_valid_header(s)) return failure("valid_header");
    ++s->bytes;
    if (rb_valid_header(s)) return failure("wrong_header_size_accepted");
    --s->bytes;
    rb_store(&s->request_write, start); rb_store(&s->request_read, start);
    rb_store(&s->reply_write, start); rb_store(&s->reply_read, start);
    memset(&observed_request, 0x53, sizeof(observed_request));
    memset(&observed_reply, 0x74, sizeof(observed_reply));
    unchanged_request = observed_request; unchanged_reply = observed_reply;
    if (rb_pop_request(s, &observed_request) != 0 ||
        memcmp(&observed_request, &unchanged_request, sizeof(observed_request)) ||
        rb_pop_reply(s, &observed_reply) != 0 ||
        memcmp(&observed_reply, &unchanged_reply, sizeof(observed_reply)))
        return failure("empty_pop_changed_destination");
    for (i = 0; i < RB_RING_CAPACITY; ++i) {
        request = make_request(i); reply = make_reply(&request, i);
        if (rb_push_request(s, &request) != 1 || rb_push_reply(s, &reply) != 1)
            return failure("ring_filled_before_capacity");
    }
    if (rb_load(&s->request_write) != start + RB_RING_CAPACITY ||
        rb_load(&s->reply_write) != start + RB_RING_CAPACITY ||
        rb_push_request(s, &request) != 0 || rb_push_reply(s, &reply) != 0)
        return failure("full_ring_or_uint32_wrap");
    for (i = 0; i < RB_RING_CAPACITY; ++i) {
        request = make_request(i); reply = make_reply(&request, i);
        if (rb_pop_request(s, &observed_request) != 1 ||
            memcmp(&request, &observed_request, sizeof(request)) ||
            rb_pop_reply(s, &observed_reply) != 1 ||
            memcmp(&reply, &observed_reply, sizeof(reply)))
            return failure("payload_or_fifo_order");
    }
    if (rb_pop_request(s, &observed_request) || rb_pop_reply(s, &observed_reply))
        return failure("drained_ring_not_empty");
    rb_store(&s->request_read, 9); rb_store(&s->request_write, 9 + RB_RING_CAPACITY + 1);
    rb_store(&s->reply_read, 9); rb_store(&s->reply_write, 9 + RB_RING_CAPACITY + 1);
    if (rb_push_request(s, &request) != -1 || rb_pop_request(s, &observed_request) != -1 ||
        rb_push_reply(s, &reply) != -1 || rb_pop_reply(s, &observed_reply) != -1 ||
        rb_load(&s->request_read) != 9 || rb_load(&s->request_write) != 9 + RB_RING_CAPACITY + 1 ||
        rb_load(&s->reply_read) != 9 || rb_load(&s->reply_write) != 9 + RB_RING_CAPACITY + 1)
        return failure("malformed_counters_not_rejected_without_advance");
    return 0;
}

static int lease_checks(RBShared *s) {
    uint32_t first, second;
    RBReply old_reply = {0}, observed;
    header_initialize(s);
    rb_store(&s->request_write, 123); rb_store(&s->request_read, 123);
    rb_store(&s->reply_write, 456); rb_store(&s->reply_read, 456);
    first = rb_claim_client(s);
    if (!first || rb_load(&s->client_token) != first || rb_claim_client(s) != 0)
        return failure("first_lease_or_competing_claim");
    rb_release_client(s, first + 1u);
    if (rb_load(&s->client_token) != first) return failure("nonowner_released_lease");
    old_reply.client_token = first;
    old_reply.epoch = 2;
    old_reply.stream_sample = 64;
    if (rb_push_reply(s, &old_reply) != 1) return failure("old_reply_setup");
    rb_release_client(s, first);
    second = rb_claim_client(s);
    if (!second || second == first || rb_load(&s->client_token) != second)
        return failure("handoff_reused_generation");
    rb_release_client(s, first);
    if (rb_load(&s->client_token) != second) return failure("late_old_owner_released_new_lease");
    rb_release_client(s, second);
    if (rb_load(&s->request_write) != 123 || rb_load(&s->request_read) != 123 ||
        rb_load(&s->reply_write) != 457 || rb_load(&s->reply_read) != 456)
        return failure("lease_handoff_reset_ring_counters");
    if (rb_pop_reply(s, &observed) != 1 || observed.client_token != first ||
        observed.client_token == second || observed.epoch != 2)
        return failure("queued_old_reply_aliases_new_owner");
    rb_store(&s->client_generation, UINT32_MAX - 1u);
    first = rb_claim_client(s);
    if (first != UINT32_MAX) return failure("last_unique_generation_refused");
    rb_release_client(s, first);
    if (rb_claim_client(s) != 0 || rb_load(&s->client_generation) != UINT32_MAX ||
        rb_load(&s->client_token) != 0)
        return failure("exhausted_generation_wrapped_or_claimed");
    return 0;
}

typedef struct {
    RBShared *shared;
    HANDLE start, cancel;
    volatile LONG inside, failed;
    uint32_t tokens[2][LEASES_PER_THREAD];
} LeaseRace;
typedef struct { LeaseRace *race; unsigned int slot; } LeaseThread;

static DWORD WINAPI lease_contender(void *argument) {
    LeaseThread *thread = argument;
    LeaseRace *race = thread->race;
    uint32_t completed = 0;
    DWORD started;
    if (WaitForSingleObject(race->start, TEST_TIMEOUT_MS) != WAIT_OBJECT_0) return 1;
    started = GetTickCount();
    while (completed < LEASES_PER_THREAD) {
        uint32_t candidate;
        if (WaitForSingleObject(race->cancel, 0) == WAIT_OBJECT_0 ||
            (DWORD)(GetTickCount() - started) >= TEST_TIMEOUT_MS) return 1;
        candidate = rb_claim_client(race->shared);
        if (!candidate) { SwitchToThread(); continue; }
        if (InterlockedIncrement(&race->inside) != 1 ||
            rb_load(&race->shared->client_token) != candidate) {
            InterlockedExchange(&race->failed, 1);
            rb_release_client(race->shared, candidate);
            return 1;
        }
        race->tokens[thread->slot][completed++] = candidate;
        if (!(completed % 16)) SwitchToThread();
        if (InterlockedDecrement(&race->inside) != 0)
            InterlockedExchange(&race->failed, 1);
        rb_release_client(race->shared, candidate);
    }
    return 0;
}

static int token_compare(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static int competing_lease_checks(RBShared *s) {
    LeaseRace race = {0};
    LeaseThread arguments[2] = {{&race, 0}, {&race, 1}};
    HANDLE threads[2] = {NULL, NULL};
    uint32_t tokens[2 * LEASES_PER_THREAD];
    unsigned int i;
    DWORD exit_code;
    int result = 1;
    header_initialize(s);
    race.shared = s;
    race.start = CreateEventW(NULL, TRUE, FALSE, NULL);
    race.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!race.start || !race.cancel) goto done;
    for (i = 0; i < 2; ++i) {
        threads[i] = CreateThread(NULL, 0, lease_contender, &arguments[i], 0, NULL);
        if (!threads[i]) goto done;
    }
    SetEvent(race.start);
    if (WaitForMultipleObjects(2, threads, TRUE, TEST_TIMEOUT_MS) != WAIT_OBJECT_0) goto done;
    for (i = 0; i < 2; ++i)
        if (!GetExitCodeThread(threads[i], &exit_code) || exit_code != 0) goto done;
    if (InterlockedCompareExchange(&race.failed, 0, 0) ||
        InterlockedCompareExchange(&race.inside, 0, 0) || rb_load(&s->client_token)) goto done;
    memcpy(tokens, race.tokens, sizeof(tokens));
    qsort(tokens, 2 * LEASES_PER_THREAD, sizeof(tokens[0]), token_compare);
    if (!tokens[0]) goto done;
    for (i = 1; i < 2 * LEASES_PER_THREAD; ++i)
        if (tokens[i] == tokens[i - 1]) goto done;
    result = 0;
done:
    if (race.cancel) SetEvent(race.cancel);
    if (race.start) SetEvent(race.start);
    for (i = 0; i < 2; ++i) {
        if (!threads[i]) continue;
        if (WaitForSingleObject(threads[i], 3000) != WAIT_OBJECT_0) {
            fprintf(stderr, "ipc_fixture_failed=lease_thread_join_timeout\n");
            fflush(NULL);
            TerminateProcess(GetCurrentProcess(), 124);
            ExitProcess(124);
        }
        CloseHandle(threads[i]);
    }
    if (race.cancel) CloseHandle(race.cancel);
    if (race.start) CloseHandle(race.start);
    return result ? failure("competing_claims_reused_token_or_shared_lease") : 0;
}

typedef struct {
    RBShared *shared;
    HANDLE cancel;
    volatile LONG failed;
} Exchange;

static DWORD WINAPI responder(void *argument) {
    Exchange *exchange = argument;
    uint32_t sequence = 0;
    DWORD started = GetTickCount();
    while (sequence < EXCHANGES) {
        RBRequest request, expected;
        RBReply reply;
        int popped;
        if (WaitForSingleObject(exchange->cancel, 0) == WAIT_OBJECT_0) return 1;
        if ((DWORD)(GetTickCount() - started) >= TEST_TIMEOUT_MS) goto failed;
        popped = rb_pop_request(exchange->shared, &request);
        if (popped < 0) goto failed;
        if (!popped) { SwitchToThread(); continue; }
        expected = make_request(sequence);
        if (memcmp(&request, &expected, sizeof(request))) goto failed;
        reply = make_reply(&request, sequence);
        for (;;) {
            int pushed = rb_push_reply(exchange->shared, &reply);
            if (pushed < 0) goto failed;
            if (pushed == 1) break;
            if (WaitForSingleObject(exchange->cancel, 0) == WAIT_OBJECT_0) return 1;
            if ((DWORD)(GetTickCount() - started) >= TEST_TIMEOUT_MS) goto failed;
            SwitchToThread();
        }
        ++sequence;
    }
    return 0;
failed:
    InterlockedExchange(&exchange->failed, 1);
    return 1;
}

static int concurrent_checks(RBShared *s) {
    Exchange exchange = {0};
    HANDLE thread;
    DWORD started, thread_exit = 1;
    uint32_t sent = 0, received = 0, start = UINT32_MAX - 63u;
    int result = 1;
    header_initialize(s);
    rb_store(&s->request_write, start); rb_store(&s->request_read, start);
    rb_store(&s->reply_write, start); rb_store(&s->reply_read, start);
    exchange.shared = s;
    exchange.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!exchange.cancel) return failure("cancel_event");
    thread = CreateThread(NULL, 0, responder, &exchange, 0, NULL);
    if (!thread) { CloseHandle(exchange.cancel); return failure("responder_thread"); }
    started = GetTickCount();
    while (received < EXCHANGES) {
        RBRequest request;
        RBReply expected, reply;
        int pushed = 0, popped;
        if ((DWORD)(GetTickCount() - started) >= TEST_TIMEOUT_MS ||
            InterlockedCompareExchange(&exchange.failed, 0, 0)) goto done;
        if (sent < EXCHANGES) {
            request = make_request(sent);
            pushed = rb_push_request(s, &request);
            if (pushed < 0) goto done;
            if (pushed == 1) ++sent;
        }
        popped = rb_pop_reply(s, &reply);
        if (popped < 0) goto done;
        if (popped == 1) {
            request = make_request(received); expected = make_reply(&request, received);
            if (memcmp(&reply, &expected, sizeof(reply))) goto done;
            ++received;
        }
        if (!pushed && !popped) SwitchToThread();
    }
    if (sent != EXCHANGES || rb_load(&s->request_write) != start + EXCHANGES ||
        rb_load(&s->request_read) != start + EXCHANGES ||
        rb_load(&s->reply_write) != start + EXCHANGES ||
        rb_load(&s->reply_read) != start + EXCHANGES) goto done;
    result = 0;
done:
    SetEvent(exchange.cancel);
    if (WaitForSingleObject(thread, 3000) != WAIT_OBJECT_0) {
        /* Only this fixture owns the thread. Never return stack/shared memory
         * while a timed-out responder might still access it. */
        fprintf(stderr, "ipc_fixture_failed=responder_join_timeout\n");
        fflush(NULL);
        TerminateProcess(GetCurrentProcess(), 124);
        ExitProcess(124);
    }
    if (!GetExitCodeThread(thread, &thread_exit) || thread_exit != 0) result = 1;
    CloseHandle(thread); CloseHandle(exchange.cancel);
    return result ? failure("concurrent_patterned_round_trip") : 0;
}
#endif

int main(void) {
#ifdef _WIN32
    RBShared *shared = calloc(1, sizeof(*shared));
    int result;
    if (!shared) return failure("allocation");
    result = serial_checks(shared);
    if (!result) result = lease_checks(shared);
    if (!result) result = competing_lease_checks(shared);
    if (!result) result = concurrent_checks(shared);
    free(shared);
    if (result) return result;
    printf("{\"scope\":\"actual_header_only\",\"pointer_bits\":%u,"
           "\"mapping_bytes\":%u,\"fifo_capacity\":128,\"uint32_wrap\":true,"
           "\"malformed_counters_rejected\":true,\"concurrent_exchanges\":16384,"
           "\"lease_handoff_nonowner_release_exhaustion_checked\":true,"
           "\"queued_old_reply_token_remains_distinct\":true,"
           "\"competing_unique_claims\":2048,"
           "\"engine_loaded\":false,\"passed\":true}\n",
           (unsigned int)(sizeof(void *) * 8), (unsigned int)sizeof(RBShared));
#else
    printf("{\"scope\":\"portable_layout_only\",\"pointer_bits\":%u,"
           "\"request_bytes\":%u,\"reply_bytes\":%u,\"mapping_bytes\":%u,"
           "\"engine_loaded\":false,\"passed\":true}\n",
           (unsigned int)(sizeof(void *) * 8), (unsigned int)sizeof(RBRequest),
           (unsigned int)sizeof(RBReply), (unsigned int)sizeof(RBShared));
#endif
    return 0;
}

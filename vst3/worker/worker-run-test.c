#include "worker-run.h"
#include <stdio.h>

static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line=%u: %s\n", (unsigned)__LINE__, #x); return 1; \
} } while (0)
int main(void) {
    RBWorkerRun run = {0};
    RBRequest request = {0};
    enum RBRunAction action;
    unsigned int unready;
    for (unready = RB_NO_SONG; unready <= RB_LOOP_ON; ++unready) {
        if (unready != RB_NO_SONG && unready != RB_LOOP_ON) continue;
        memset(&run, 0, sizeof(run));
        memset(&request, 0, sizeof(request));
        rb_run_lease(&run, 11);
        request.client_token = 11; request.epoch = 1; request.playing = 1;
        action = rb_run_decide(&run, &request, unready);
        CHECK(action == RB_RUN_BLOCKED);
        rb_run_commit(&run, &request, action);
        CHECK(run.rearm_required && !run.playing);
        request.engine_frame = 64;
        CHECK(rb_run_decide(&run, &request, RB_READY) == RB_RUN_BLOCKED);
        request.engine_frame = 0;
        CHECK(rb_run_decide(&run, &request, RB_READY) == RB_RUN_BLOCKED);
        request.playing = 0;
        action = rb_run_decide(&run, &request, RB_READY);
        CHECK(action == RB_RUN_STOP);
        rb_run_commit(&run, &request, action);
        CHECK(!run.rearm_required && !run.playing);
        request.playing = 1; request.epoch = 2;
        action = rb_run_decide(&run, &request, RB_READY);
        CHECK(action == RB_RUN_PLAY);
        rb_run_commit(&run, &request, action);
        CHECK(run.playing && run.next_frame == 64);
        request.engine_frame = 64;
        action = rb_run_decide(&run, &request, RB_READY);
        CHECK(action == RB_RUN_PLAY);
        rb_run_commit(&run, &request, action);
        CHECK(run.next_frame == 128);
        rb_run_stale(&run);
        CHECK(!run.playing && run.rearm_required);
        request.engine_frame = 128;
        CHECK(rb_run_decide(&run, &request, RB_READY) == RB_RUN_BLOCKED);
        /* A new owner with the SAME epoch must not inherit the old run. */
        rb_run_lease(&run, 22);
        request.client_token = 22; request.engine_frame = 0;
        CHECK(!run.rearm_required && !run.playing && run.lease == 22);
        CHECK(rb_run_decide(&run, &request, RB_READY) == RB_RUN_PLAY);
        rb_run_commit(&run, &request, RB_RUN_PLAY);
        request.engine_frame = 128;
        CHECK(rb_run_decide(&run, &request, RB_READY) == RB_RUN_BLOCKED);
        rb_run_lease(&run, 0);
        CHECK(!run.playing && !run.rearm_required && !run.epoch && !run.next_frame);
    }
    printf("worker_run_checks=%u passed; engine_calls=0\n", checks);
    return 0;
}

/* Pure transport state machine shared by the worker and its no-engine test. */
#ifndef RB_WORKER_RUN_H
#define RB_WORKER_RUN_H
#include "../shared/bridge_ipc.h"
typedef struct {
    uint32_t lease, epoch;
    uint64_t next_frame;
    uint32_t playing, rearm_required;
} RBWorkerRun;
enum RBRunAction { RB_RUN_STOP = 0, RB_RUN_PLAY = 1, RB_RUN_BLOCKED = 2 };
static inline void rb_run_lease(RBWorkerRun *run, uint32_t lease) {
    if (lease != run->lease) {
        memset(run, 0, sizeof(*run));
        run->lease = lease;
    }
}
static inline void rb_run_stale(RBWorkerRun *run) {
    if (run->playing) {
        run->playing = 0; run->epoch = 0; run->next_frame = 0;
        run->rearm_required = 1;
    }
}
static inline enum RBRunAction rb_run_decide(const RBWorkerRun *run,
        const RBRequest *request, uint32_t ready_state) {
    uint64_t expected = run->playing && run->epoch == request->epoch ? run->next_frame : 0;
    if (!request->playing) return RB_RUN_STOP;
    if (run->rearm_required || request->engine_frame != expected ||
        (ready_state != RB_READY && ready_state != RB_PLAYING)) return RB_RUN_BLOCKED;
    return RB_RUN_PLAY;
}
static inline void rb_run_commit(RBWorkerRun *run, const RBRequest *request,
                                 enum RBRunAction action) {
    run->epoch = request->epoch;
    run->playing = action == RB_RUN_PLAY;
    run->rearm_required = action == RB_RUN_BLOCKED;
    run->next_frame = run->playing ? request->engine_frame + RB_BLOCK_FRAMES : 0;
}
#endif

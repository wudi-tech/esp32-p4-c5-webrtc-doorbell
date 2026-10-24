#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "door_lock_state.h"

static void expect_transition(door_lock_state_t from,
                              door_lock_event_t event,
                              door_lock_state_t expected)
{
    door_lock_state_t actual = door_lock_state_transition(from, event);
    assert(actual == expected);
}

int main(void)
{
    expect_transition(DOOR_LOCK_STATE_LOCKED, DOOR_LOCK_EVENT_OPEN_REQUEST,
                      DOOR_LOCK_STATE_OPENING);
    expect_transition(DOOR_LOCK_STATE_OPENING, DOOR_LOCK_EVENT_OPEN_REQUEST,
                      DOOR_LOCK_STATE_OPENING);
    expect_transition(DOOR_LOCK_STATE_OPENING, DOOR_LOCK_EVENT_MOTION_COMPLETE,
                      DOOR_LOCK_STATE_UNLOCKED);
    expect_transition(DOOR_LOCK_STATE_UNLOCKED, DOOR_LOCK_EVENT_OPEN_REQUEST,
                      DOOR_LOCK_STATE_UNLOCKED);
    expect_transition(DOOR_LOCK_STATE_UNLOCKED, DOOR_LOCK_EVENT_HOLD_EXPIRED,
                      DOOR_LOCK_STATE_CLOSING);
    expect_transition(DOOR_LOCK_STATE_CLOSING, DOOR_LOCK_EVENT_OPEN_REQUEST,
                      DOOR_LOCK_STATE_CLOSING);
    expect_transition(DOOR_LOCK_STATE_CLOSING, DOOR_LOCK_EVENT_MOTION_COMPLETE,
                      DOOR_LOCK_STATE_LOCKED);

    for (int state = DOOR_LOCK_STATE_LOCKED;
         state <= DOOR_LOCK_STATE_CLOSING; ++state) {
        expect_transition((door_lock_state_t)state, DOOR_LOCK_EVENT_FAILURE,
                          DOOR_LOCK_STATE_FAULT);
    }
    expect_transition(DOOR_LOCK_STATE_FAULT, DOOR_LOCK_EVENT_OPEN_REQUEST,
                      DOOR_LOCK_STATE_FAULT);

    assert(strcmp(door_lock_state_name(DOOR_LOCK_STATE_LOCKED), "LOCKED") == 0);
    assert(strcmp(door_lock_state_name(DOOR_LOCK_STATE_OPENING), "OPENING") == 0);
    assert(strcmp(door_lock_state_name(DOOR_LOCK_STATE_UNLOCKED), "UNLOCKED") == 0);
    assert(strcmp(door_lock_state_name(DOOR_LOCK_STATE_CLOSING), "CLOSING") == 0);
    assert(strcmp(door_lock_state_name(DOOR_LOCK_STATE_FAULT), "FAULT") == 0);

    puts("PASS: door lock state transitions");
    return 0;
}

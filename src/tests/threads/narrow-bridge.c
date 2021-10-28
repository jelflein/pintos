/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

void ArriveBridge(char direc,char prio);
void CrossBridge(char direc,char prio);
void ExitBridge(char direc,char prio);
void init();

static struct semaphore cars_right;
static struct semaphore cars_left;

static struct semaphore bridge_capacity;

static struct semaphore prio_left;
static struct semaphore prio_right;

//0 = left, 1 = right
static uint8_t current_direc;


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);


void test_narrow_bridge(void)
{
    /*narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);*/
    narrow_bridge(0, 10, 10, 0);
    pass();
}

void init() {
    sema_init(&waiting_for_other_direc, 0);
    sema_init(&bridge_capacity, 3);
    sema_init(&prio_other_direc, 0);

    current_direc = 0;
}

void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
    init();
    msg("NOT IMPLEMENTED");
    /* FIXME implement */
}

void ArriveBridge(char direc, char prio) {
    if(prio) {
        if(direc == 0 && bridge_capacity.value != 0) sema_down(&prio_left);
        else if(bridge_capacity.value != 0) sema_down(&prio_right);

        CrossBridge(direc, 0);
        return;
    }

    if(current_direc != direc) {
        if(direc == 0 && bridge_capacity.value != 0) {
            sema_down(&cars_left);
        } else if(direc == 0) {
            current_direc = 0;
        }

        if(direc == 1 && bridge_capacity.value != 0) {
            sema_down(&cars_right);
        } else if(direc == 1) {
            current_direc = 1;
        }
    }

    sema_down(&bridge_capacity);
    CrossBridge(direc, prio);
}

void CrossBridge(char direc,char prio) {
    //sleep
    ExitBridge(direc, prio);
}

void ExitBridge(char direc,char prio) {
    if(current_direc == 0 && !list_empty(&prio_left.waiters)) {
        sema_up(&prio_left);
    }

    if(current_direc == 1 && !list_empty(&prio_right.waiters)) {
        sema_up(&prio_right);
    }

    sema_up(&bridge_capacity);

    if(bridge_capacity.value == 3 && list_empty(&bridge_capacity.waiters)) {
        struct semaphore invert_direc_sema;

        if(current_direc == 0) {
            invert_direc_sema = cars_right;
            current_direc = 1;
        } else {
            invert_direc_sema = cars_left;
            current_direc = 0;
        }

        while (!list_empty(&invert_direc_sema.waiters)) {
            sema_up(&invert_direc_sema);
        }
    }
}

/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "devices/timer.h"
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

void ArriveBridge(char direc, bool prio);

void CrossBridge(char direc, bool prio);

void ExitBridge(char direc, bool prio);

void init(void);

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
    narrow_bridge(7, 4, 0, 0);
    pass();
}

void init() {
    sema_init(&mutex, 1);

    sema_init(&okToDriveLeft, 0);
    sema_init(&okToDriveRight, 0);

    bridge_direc = 0;
    driving_left_count = 0;
    driving_left_count = 0;

    waiting_cars_left = 0;
    waiting_cars_right = 0;

    bridge_capi = 0;
}

enum car_purpose {
    LEFT,
    EMERGENCY_LEFT,
    RIGHT,
    EMERGENCY_RIGHT
};

void car_thread(UNUSED void *info);

void car_thread(UNUSED void *info) {
    enum car_purpose purpose = (enum car_purpose) info;

    msg("thread starting %s with purpose %u\n", thread_current()->name, purpose);

    unsigned char direction = (purpose == RIGHT) || (purpose == EMERGENCY_RIGHT);
    bool hasPrio = (purpose == EMERGENCY_LEFT) || (purpose == EMERGENCY_RIGHT);

    ArriveBridge(direction, hasPrio);
}

void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
    init();

    enum car_purpose purpose = LEFT;

    for (unsigned i = 0; i < num_vehicles_left; i++) {
        char name[32];
        snprintf(name, sizeof name, "car left %d", i);
        thread_create(name, PRI_DEFAULT, car_thread, (void *) purpose);
    }

    purpose = RIGHT;
    for (unsigned i = 0; i < num_vehicles_right; i++) {
        char name[32];
        snprintf(name, sizeof name, "car right %d", i);
        thread_create(name, PRI_DEFAULT, car_thread, (void *) purpose);
    }

    purpose = EMERGENCY_LEFT;
    for (unsigned i = 0; i < num_emergency_left; i++) {
        char name[32];
        snprintf(name, sizeof name, "emergen left %d", i);
        thread_create(name, PRI_DEFAULT, car_thread, (void *) purpose);
    }

    purpose = EMERGENCY_RIGHT;
    for (unsigned i = 0; i < num_emergency_right; i++) {
        char name[32];
        snprintf(name, sizeof name, "emergen right %d", i);
        thread_create(name, PRI_DEFAULT, car_thread, (void *) purpose);
    }
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
    } else //right
    {
        if (waiting_cars_left + driving_left_count == 0 && driving_right_count < LIMIT_CARS) {
            sema_up(&okToDriveRight);
            driving_right_count++;
        } else {
            waiting_cars_right++;
        }
    }

    sema_down(&bridge_capacity);
    CrossBridge(direc, prio);
}

void CrossBridge(char direc,char prio) {
    //sleep
    ExitBridge(direc, prio);
}

void ExitBridge(char direc, bool prio) {
    sema_down(&mutex);

    msg("EXIT: %s\n", thread_current()->name);

    if (direc == 0) //left
    {
        driving_left_count--;

        if (waiting_cars_left > 0) {
            sema_up(&okToDriveLeft);
            driving_left_count++;
            waiting_cars_left--;
        }

        if (driving_left_count == 0) {
            while (waiting_cars_right > 0 && driving_right_count < LIMIT_CARS) {
                sema_up(&okToDriveRight);
                driving_right_count++;
                waiting_cars_right--;
            }
        }
    } else { //right
        driving_right_count--;

        if (waiting_cars_right > 0) {
            sema_up(&okToDriveRight);
            driving_right_count++;
            waiting_cars_right--;
        }

        if (driving_right_count == 0) {
            while (waiting_cars_left > 0 && driving_left_count < LIMIT_CARS) {
                sema_up(&okToDriveLeft);
                driving_left_count++;
                waiting_cars_left--;
            }
        }
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

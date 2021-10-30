/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include <random.h>
#include "devices/timer.h"
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

void ArriveBridge(char direc, bool prio);

void CrossBridge(char direc, bool prio);

void ExitBridge(char direc, bool prio);

void init(void);

static uint8_t waiting_cars_right;
static uint8_t waiting_cars_left;

static struct semaphore mutex;
static struct semaphore okToDriveLeft;
static struct semaphore okToDriveRight;

#define LIMIT_CARS 3

static uint8_t driving_left_count;
static uint8_t driving_right_count;

static bool change_direc;


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
                   unsigned int num_emergency_left, unsigned int num_emergency_right);


void test_narrow_bridge(void) {
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
    narrow_bridge(11, 4, 0, 3);
    pass();
}

void init() {
    sema_init(&mutex, 1);

    sema_init(&okToDriveLeft, 0);
    sema_init(&okToDriveRight, 0);

    driving_left_count = 0;
    driving_right_count = 0;

    waiting_cars_left = 0;
    waiting_cars_right = 0;

    change_direc = false;
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

    if (hasPrio) {
        //timer_msleep(6000);
        msg("%s now waiting\n", thread_current()->name);
    }

    ArriveBridge(direction, hasPrio);
}

void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
                   unsigned int num_emergency_left, unsigned int num_emergency_right) {
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

void ArriveBridge(char direc, bool prio) {
    sema_down(&mutex);

    if (direc == 0) //left
    {
        if (waiting_cars_right + driving_right_count == 0 && driving_left_count < LIMIT_CARS) {
            sema_up(&okToDriveLeft);
            driving_left_count++;
        } else {
            waiting_cars_left++;
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

    sema_up(&mutex);


    CrossBridge(direc, prio);
}

void CrossBridge(char direc, bool prio) {
    if (direc == 0) { //left
        if (prio) {
            sema_down(&mutex);

            if (driving_right_count > 0) {
                //change direc
                change_direc = true;
            }

            sema_up(&mutex);

            sema_down_push_front(&okToDriveLeft);
        }
        else sema_down(&okToDriveLeft);
    } else { //right
        if (prio) {
            sema_down(&mutex);

            if (driving_left_count > 0) {
                //change direc
                change_direc = true;
            }

            sema_up(&mutex);

            sema_down_push_front(&okToDriveRight);
        }
        else sema_down(&okToDriveRight);
    }

    msg("CROSSING: %s\n", thread_current()->name);
    timer_msleep(1000 + (random_ulong() % 2000));

    ExitBridge(direc, prio);
}

void ExitBridge(char direc, UNUSED bool prio) {
    msg("EXIT: %s\n", thread_current()->name);

    sema_down(&mutex);

    if (direc == 0) //left
    {
        driving_left_count--;

        if (waiting_cars_left > 0 && !change_direc) {
            sema_up(&okToDriveLeft);
            driving_left_count++;
            waiting_cars_left--;
        }

        if (driving_left_count == 0) {
            if (change_direc) change_direc = 0;

            while (waiting_cars_right > 0 && driving_right_count < LIMIT_CARS) {
                sema_up(&okToDriveRight);
                driving_right_count++;
                waiting_cars_right--;
            }
        }
    } else { //right
        driving_right_count--;

        if (waiting_cars_right > 0 && !change_direc) {
            sema_up(&okToDriveRight);
            driving_right_count++;
            waiting_cars_right--;
        }

        if (driving_right_count == 0) {
            if (change_direc) change_direc = 0;

            while (waiting_cars_left > 0 && driving_left_count < LIMIT_CARS) {
                sema_up(&okToDriveLeft);
                driving_left_count++;
                waiting_cars_left--;
            }
        }
    }

    sema_up(&mutex);
}

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

struct semaphore waiting_for_prio;
static struct semaphore bridge_capacity;
static uint8_t capacity;

//0 = left, 1 = right
static uint8_t current_direc;

struct semaphore bridge;

static struct list car_threads;


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
    sema_init(&waiting_for_prio, 0);
    sema_init(&bridge_capacity, 3);

    sema_init(&bridge, 0);

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
    sema_down(&bridge);

    if(prio) {
        //prüfen ob alles i.O. ist
        if(current_direc == direc && capacity < 3) {
            capacity++;
            CrossBridge(direc, 0);
        } else {
            //wenn nicht flag setzen, dass keine mehr angenommen werden
            sema_down(&waiting_for_prio);
        }
    }

    if(current_direc != direc) {
        //prüfen ob richtung geändert werden kann
        //wenn nicht sleep
    }

    sema_down(&bridge_capacity);

    CrossBridge(direc, 0);
}

void CrossBridge(char direc,char prio) {

}

void ExitBridge(char direc,char prio) {
    sema_down(&bridge);

    sema_up(&bridge_capacity);
    sema_up(&bridge);
}

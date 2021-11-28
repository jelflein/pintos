/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include <string.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"


void producer_consumer(unsigned int num_producer, unsigned int num_consumer);


void test_producer_consumer(void)
{
    /*producer_consumer(0, 0);
    producer_consumer(1, 0);
    producer_consumer(0, 1);
    producer_consumer(1, 1);
    producer_consumer(3, 1);
    producer_consumer(1, 3);
    producer_consumer(4, 4);
    producer_consumer(7, 2);*/
    producer_consumer(2, 7);
    //producer_consumer(6, 6);
    pass();
}


static char buffer[16];
static unsigned int head;
static unsigned int tail;
static volatile bool is_full;

static struct lock buffer_lock;

static struct condition not_full_cond;
static struct condition not_empty_cond;

void init_producer_consumer(void);

void init_producer_consumer(void)
{
    head = 0;
    tail = 0;
    is_full = false;

    lock_init(&buffer_lock);

    cond_init(&not_full_cond);
    cond_init(&not_empty_cond);

    for (unsigned i = 0; i < sizeof(buffer) / sizeof(buffer[0]); i++)
    {
        buffer[i] = '#';
    }
}

bool is_buffer_empty(void);

bool is_buffer_empty(void)
{
    return head == tail && !is_full;
}

/*
 * returns 1 if buffer full
 * */
bool buffer_append(char c);

bool buffer_append(char c)
{
    ASSERT(is_full == 0);

    buffer[tail] = c;
    tail++;

    if (tail > (sizeof(buffer) / sizeof(buffer[0]) - 1)) {
        tail = 0;
    }

    if (head == tail)
    {
        is_full = true;
    }

    return is_full;
}

void buffer_append_synchronized(char c);

void buffer_append_synchronized(char c)
{
    //msg ("acquire bufferlock %s \n", thread_current()->name);
    lock_acquire(&buffer_lock);

    while (is_full)
    {
        //msg ("wait for not full %16c \n", buffer);
        //msg ("wait for not full %s \n", thread_current()->name);
        cond_wait(&not_full_cond, &buffer_lock);
        //msg ("wating is over for not full %s \n", thread_current()->name);
    }

    buffer_append(c);
    msg("write %c from %s\n", c, thread_current()->name);

    //msg ("signal not empty %s \n", thread_current()->name);
    cond_signal(&not_empty_cond, &buffer_lock);

    //msg ("release bufferlock %s \n", thread_current()->name);
    lock_release(&buffer_lock);
}

char buffer_take_front(void);

char buffer_take_front()
{
    ASSERT(!is_buffer_empty());

    char ret = buffer[head];
    head++;

    if (head == (sizeof(buffer) / sizeof(buffer[0]))) {
        head = 0;
    }

    if (is_full) {
        is_full = 0;
    }

    return ret;
}
char buffer_take_front_synchronized(void);

char buffer_take_front_synchronized()
{
    //msg ("acquire bufferlock %s \n", thread_current()->name);
    lock_acquire(&buffer_lock);

    while (is_buffer_empty()) {
        //msg ("wait for not empty %s \n", thread_current()->name);
        //msg ("wait for not empty %16s \n", buffer);

        cond_wait(&not_empty_cond, &buffer_lock);
        //msg ("wating is over for not empty %s \n", thread_current()->name);
    }

    char ret = buffer_take_front();

    //msg ("signal not full %s \n", thread_current()->name);
    cond_signal(&not_full_cond, &buffer_lock);

    //msg ("release bufferlock %s \n", thread_current()->name);
    lock_release(&buffer_lock);

    return ret;
}

void place_char(char c);

void place_char(char c)
{
    return buffer_append_synchronized(c);
}

char pull_character(void);

char pull_character()
{
    return buffer_take_front_synchronized();
}

void producer_thread(UNUSED void *info);

void producer_thread(UNUSED void *info)
{
    const char *msg = "Hello world";
    for (unsigned i = 0; i < strlen(msg); i++)
    {
        place_char(msg[i]);
    }
}

_Noreturn void consumer_thread(UNUSED void *info);

_Noreturn void consumer_thread(UNUSED void *info)
{
    // read characters forever. If no character available, wait until there is
    for (;;) {
        msg("%c from %s\n", pull_character(), thread_current()->name);
    }
}

void producer_consumer(unsigned int num_producer, unsigned int num_consumer)
{
    /* FIXME implement */
    init_producer_consumer();
    /* Start producers. */
    for (unsigned i = 0; i < num_producer; i++)
    {
        char name[32];
        snprintf (name, sizeof name, "producer %d", i);
        thread_create(name, PRI_DEFAULT, producer_thread, NULL);
    }

    /* Start consumers. */
    for (unsigned int i = 0; i < num_consumer; i++)
    {
        char name[32];
        snprintf (name, sizeof name, "consumer %d", i);
        thread_create(name, PRI_DEFAULT, consumer_thread, NULL);
    }

    timer_sleep(500);

    /* Cya. */
    msg ("main thread is gone, exiting\n");

}



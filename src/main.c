/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "console/console.h"
#include "shell/shell.h"
#include <assert.h>
#include <string.h>
#ifdef ARCH_sim
#include <mcu/mcu_sim.h>
#endif

/* Function Initializations*/
volatile int tasks_initialized;
int init_tasks(void);
int print_task_data(void); 
void print_double(char*, double n, char*); 
void fake_work_function(int);

/* Task A */
#define A_TASK_PRIO        (3)
#define A_STACK_SIZE       OS_STACK_ALIGN(256)
struct os_task a_task;
os_stack_t a_stack[A_STACK_SIZE];
static volatile int a_loops;
os_time_t a_time_offset = 0;

/* Task B */
#define B_TASK_PRIO          (2)
#define B_STACK_SIZE         OS_STACK_ALIGN(256)
struct os_task b_task;
os_stack_t b_stack[B_STACK_SIZE];
static volatile int b_loops;
os_time_t b_time_offset = 0;

/* Timer Task */
#define TIMER_TASK_PRIO         (1)
#define TIMER_STACK_SIZE        OS_STACK_ALIGN(256)
#define TIMER_LENGTH_SEC        (20)
struct os_task timer_task;
os_stack_t timer_stack[TIMER_STACK_SIZE];

/* Shell Task */
#define SHELL_TASK_PRIO         (0)
#define SHELL_MAX_INPUT_LEN     (256)
#define SHELL_TASK_STACK_SIZE   (OS_STACK_ALIGN(312))
os_stack_t shell_stack[SHELL_TASK_STACK_SIZE];

/* Testing Parameters */
#define A_DELAY                 (100)
#define A_LOOP_SIZE             (10000)
#define B_DELAY                 (3000)
#define B_LOOP_SIZE             (1000000)

#define VERBOSE (0)

/* To reset the tasks */
volatile int restart_a = 0;
volatile int restart_b = 0;

void
a_task_handler(void *arg)
{
    struct os_task *t;

    while (1) {
        t = os_sched_get_current_task();
        assert(t->t_func == a_task_handler);
        int i;
        for(i = 0; i < A_LOOP_SIZE; ++i) {
            if(restart_a) break;
            ++a_loops;
            // Simulate doing a noticeable amount of work
            fake_work_function(i);
        }
        if(restart_a) {
            restart_a = 0;
            continue;
        }
        if(VERBOSE) console_printf("   %s looped\n", t->t_name);
        os_time_delay(A_DELAY);
    }
}

void
b_task_handler(void *arg)
{
    struct os_task *t;

    while (1) {
        t = os_sched_get_current_task();
        assert(t->t_func == b_task_handler);
        int i;
        for(i = 0; i < B_LOOP_SIZE; ++i) {
            if(restart_b) break;
            ++b_loops;
            // Simulate doing a noticeable amount of work if verbose is off
            if(!VERBOSE) fake_work_function(i);
            // Print updates if verbose is set. Replaces fake_work_function
            if (VERBOSE && i % 10000 == 0) {
                console_printf("     %s: %d%% \n", t->t_name, i/10000);
            }
        }
        if(restart_b) {
            restart_b = 0;
            continue;
        }
        os_time_delay(B_DELAY);
    }
}

void
timer_task_handler(void *arg)
{
    struct os_task *t;

    while (1) {
        t = os_sched_get_current_task();
        assert(t->t_func == timer_task_handler);
        
        console_printf("   Starting First Simulation...\n");
        /* Wait for tasks to run */
        os_time_delay(OS_TICKS_PER_SEC * TIMER_LENGTH_SEC);
        /* Print data and save total loops */
        int total1 = print_task_data();
        /* Save initial runtime */
        a_time_offset = a_task.t_run_time;
        b_time_offset = b_task.t_run_time;
        
        /* Change Priorities */
        a_task.t_prio = 2;
        os_sched_resort(&a_task);
        b_task.t_prio = 3;
        os_sched_resort(&b_task);

        /* Reset Tasks */
        console_printf("   Switching priorities and restarting...\n");
        a_loops = 0;
        b_loops = 0;
        restart_a = 1;
        restart_b= 1;

        /* Wait for tasks to run again*/
        os_time_delay(OS_TICKS_PER_SEC * TIMER_LENGTH_SEC);
        int total2 = print_task_data();
        /* Print final speedup */
        double speedup = ((double)total2/total1);
        print_double("\n\n Final Speedup (Sim2 / Sim1): ", speedup, "");
        
        while(1) {}
    }
}

/**
 * fake_work_funciton
 *
 * Used to slow down the loops to notice the amount of work
 * being done.
 *
 */
void 
fake_work_function(int i)
{
    if (i % 10000 == 0) {
        asm("");
    }
}

/**
 * print_double
 *
 * Print a double to two precision points. This function is used to 
 * circumvent printf's stack alignment issues.
 *
 * @return void
 */
void
print_double(char *before, double n, char *after)
{
    int decimal = (n*100.0)-(((int)n)*100);
    console_printf("%s%d.%d%s", before, (int)n, decimal, after);
}

/**
 * print_task_data
 *
 * print simulation data from a_task and b_task
 *
 * @return void
 */
int
print_task_data() 
{
    console_printf("\n========== Timer Expired ==========");
    console_printf("\n\n >>> %s <<<", a_task.t_name);
    console_printf("\n  Priority: %d", a_task.t_prio);
    console_printf("\n  Loop count: %d", a_loops);
    print_double("\n  Cycle count: ", ((double)a_loops/A_LOOP_SIZE), "");
    print_double("\n  Run time: ", 
            ((double)(a_task.t_run_time - a_time_offset)/1000.0),
            " sec");
    console_printf("\n\n >>> %s <<<", b_task.t_name);
    console_printf("\n  Priority: %d", b_task.t_prio);
    console_printf("\n  Loop count: %d", b_loops);
    print_double("\n  Cycle count: ", ((double)b_loops/B_LOOP_SIZE), "");
    print_double("\n  Run time: ", 
            ((double)(b_task.t_run_time - b_time_offset)/1000.0),
            " sec");
    console_printf("\n\n Total loops: %d\n\n", a_loops + b_loops);
    return a_loops + b_loops;
}



/**
 * init_tasks
 *
 * Called by main.c after os_init(). This function performs initializations
 * that are required before tasks are running.
 *
 * @return int 0 success; error otherwise.
 */
int
init_tasks(void)
{
    os_task_init(&a_task, "Task A", a_task_handler, NULL,
            A_TASK_PRIO, OS_WAIT_FOREVER, a_stack, A_STACK_SIZE);
    os_task_init(&b_task, "Task B", b_task_handler, NULL,
            B_TASK_PRIO, OS_WAIT_FOREVER, b_stack, B_STACK_SIZE);
    os_task_init(&timer_task, "timer", timer_task_handler, NULL,
            TIMER_TASK_PRIO, OS_WAIT_FOREVER, timer_stack, TIMER_STACK_SIZE);
   
    /* Shell/Console */
    shell_task_init(SHELL_TASK_PRIO, shell_stack, SHELL_TASK_STACK_SIZE,
                         SHELL_MAX_INPUT_LEN);
    console_init(shell_console_rx_cb);
    assert(console_is_init());

    tasks_initialized = 1;

    return 0;
}

/**
 * main
 *
 * The main function for the project. This function initializes the os, calls
 * init_tasks to initialize tasks (and possibly other objects), then starts the
 * OS. We should not return from os start.
 *
 * @return int NOTE: this function should never return!
 */
int
main(int argc, char **argv)
{
    int rc;

#ifdef ARCH_sim
    mcu_sim_parse_args(argc, argv);
#endif

    os_init();
    rc = init_tasks();
    assert(rc == 0);
    //console_printf("Tasks initialized.");
    os_start();

    /* os start should never return. If it does, this should be an error */
    assert(0);

    return rc;
}


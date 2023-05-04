/*
 * student.c
 * Multithreaded OS Simulation for CS 2200 and ECE 3058
 *
 * This file contains the CPU scheduler for the simulation.
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "os-sim.h"

/** Function prototypes **/
extern void idle(unsigned int cpu_id);
extern void preempt(unsigned int cpu_id);
extern void yield(unsigned int cpu_id);
extern void terminate(unsigned int cpu_id);
extern void wake_up(pcb_t *process);

// Used in main function at end of code
int strcmp(const char*, const char*);

// Structure of a node that is used in the queue
typedef struct _Node {
    struct _Node* next;
    pcb_t* data;
} Node;

/*
 * current[] is an array of pointers to the currently running processes.
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 */
static pcb_t **current;
static pthread_mutex_t current_mutex;

// Variables used throughout the code
static Node* queue = NULL;
static pthread_cond_t queue_cond;
static pthread_mutex_t queue_mutex;
static int time_slice = 0;

// Allocates space for a node
static Node* createNode() {
    Node *res = (Node *) malloc(sizeof(Node));
    return res;
}

// Creates a new node and pushes it to end of queue
static void push(pcb_t* data) {
    pthread_mutex_lock(&queue_mutex);

    Node *node = createNode();
    node->next = NULL;
    node->data = data;

    Node *curr = queue;
    if (curr != NULL) {
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = node;
    } else {
        queue = node;
    }

    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

// Removes node from front of queue and frees it
// Returns data that was in the node
static pcb_t* pop() {
    pthread_mutex_lock(&queue_mutex);

    pcb_t *res = NULL;
    Node *first;

    if (queue != NULL) {
        res = queue->data;
        first = queue;
        queue = queue->next;
        free(first);
    }

    pthread_mutex_unlock(&queue_mutex);

    return res;
}

/*
 * schedule() is your CPU scheduler.  It should perform the following tasks:
 *
 *   1. Select and remove a runnable process from your ready queue which 
 *	you will have to implement with a linked list or something of the sort.
 *
 *   2. Set the process state to RUNNING
 *
 *   3. Call context_switch(), to tell the simulator which process to execute
 *      next on the CPU.  If no process is runnable, call context_switch()
 *      with a pointer to NULL to select the idle process.
 *	The current array (see above) is how you access the currently running process indexed by the cpu id. 
 *	See above for full description.
 *	context_switch() is prototyped in os-sim.h. Look there for more information 
 *	about it and its parameters.
 */
static void schedule(unsigned int cpu_id)
{
    // Pop first node in queue
    pcb_t *currProcess = pop();

    // Change process state if queue is not empty
    if (currProcess != NULL) {
        currProcess->state = PROCESS_RUNNING;
    }

    pthread_mutex_lock(&current_mutex);
    current[cpu_id] = currProcess;
    pthread_mutex_unlock(&current_mutex);

    context_switch(cpu_id, currProcess, time_slice);
}


/*
 * idle() is your idle process.  It is called by the simulator when the idle
 * process is scheduled.
 *
 * This function should block until a process is added to your ready queue.
 * It should then call schedule() to select the process to run on the CPU.
 */
extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&queue_mutex);
    
    // If queue is empty, wait until it is not empty
    if (queue == NULL) {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    pthread_mutex_unlock(&queue_mutex);

    schedule(cpu_id);

    /*
     * REMOVE THE LINE BELOW AFTER IMPLEMENTING IDLE()
     *
     * idle() must block when the ready queue is empty, or else the CPU threads
     * will spin in a loop.  Until a ready queue is implemented, we'll put the
     * thread to sleep to keep it from consuming 100% of the CPU time.  Once
     * you implement a proper idle() function using a condition variable,
     * remove the call to mt_safe_usleep() below.
     */
    //mt_safe_usleep(1000000);
}


/*
 * preempt() is the handler called by the simulator when a process is
 * preempted due to its timeslice expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, and call schedule() to select a new runnable process.
 */
extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t *process = current[cpu_id];
    pthread_mutex_unlock(&current_mutex);

    // Change process state and add to queue
    process->state = PROCESS_READY;
    push(process);
    schedule(cpu_id);
}


/*
 * yield() is the handler called by the simulator when a process yields the
 * CPU to perform an I/O request.
 *
 * It should mark the process as WAITING, then call schedule() to select
 * a new process for the CPU.
 */
extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_WAITING;
    pthread_mutex_unlock(&current_mutex);

    schedule(cpu_id);
}


/*
 * terminate() is the handler called by the simulator when a process completes.
 * It should mark the process as terminated, then call schedule() to select
 * a new process for the CPU.
 */
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);

    schedule(cpu_id);
}


/*
 * wake_up() is the handler called by the simulator when a process's I/O
 * request completes.  It should perform the following tasks:
 *
 *   1. Mark the process as READY, and insert it into the ready queue.
 *
 *   2. If the scheduling algorithm is LRTF, wake_up() may need
 *      to preempt the CPU with lower remaining time left to allow it to
 *      execute the process which just woke up with higher reimaing time.
 * 	However, if any CPU is currently running idle,
* 	or all of the CPUs are running processes
 *      with a higher remaining time left than the one which just woke up, wake_up()
 *      should not preempt any CPUs.
 *	To preempt a process, use force_preempt(). Look in os-sim.h for 
 * 	its prototype and the parameters it takes in.
 */
extern void wake_up(pcb_t *process)
{
    process->state = PROCESS_READY;
    push(process);
}


/*
 * main() simply parses command line arguments, then calls start_simulator().
 * You will need to modify it to support the -l and -r command-line parameters.
 */
int main(int argc, char *argv[])
{
    unsigned int cpu_count;

    /* Parse command-line arguments */
    if (argc < 2)
    {
        fprintf(stderr, "Multithreaded OS Simulator\n"
            "Usage: ./os-sim <# CPUs> [ -l | -r <time slice> ]\n"
            "    Default : FIFO Scheduler\n"
	    "         -l : Longest Remaining Time First Scheduler\n"
            "         -r : Round-Robin Scheduler\n\n");
        return -1;
    }

    // Check for additional argument
    if (argc > 2) {
        for (int i = 2; i < argc; ++i) {
            if (i < argc - 1 && !strcmp("-r", argv[i])) {
                time_slice = atoi(argv[i + 1]);
            }
        }
    }

    cpu_count = strtoul(argv[1], NULL, 0);

    /* Allocate and initialize the current[] array and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);

    for (int i = 0; i < cpu_count; ++i) {
        current[i] = NULL;
    }

    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_cond, NULL);

    /* Start the simulator in the library */
    start_simulator(cpu_count);

    pthread_mutex_destroy(&current_mutex);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_cond);

    return 0;
}

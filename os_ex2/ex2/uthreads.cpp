#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <queue>
#include <stdlib.h>
#include <vector>
#include <map>
#include "uthreads.h"
#include "Thread.h"
#include <iostream>

#define RUNNING_SIZE 1
#define ERR -1
#define RUNNING 3
#define BLOCKED 2
#define READY 1
#define NOT_INIT 0
//#define SUCC 0
#define MIN_SIZE 1
#define MICRO_TO_SEC 1000000
#define MAIN_THREAD_ID 0
#define ALARM 0
#define SELF_TERMINATE 1
#define SELF_BLOCK 2
#define SYSTEM_ERR "system error: "
#define LIB_ERR "thread library error: "
#define REAL_TIMER 0
#define REMAIN_TIMER 1
#define RESET_TIMER 2
#define RELEASE_MASK 0
#define SET_MASK 1


//program DS:
static Thread *running;
std::deque<Thread *> ready;
static std::vector<Thread *> blocked;
static std::vector<int> priority_array;

//map between id and threads in order to make changes easily by tid(like priorities,get information
// of threads(blocked,running,ready))
static std::map<int, Thread *> threads_map;

//will hold all the available id's
static std::priority_queue<int, std::vector<int>, std::greater<int>> ids;

//for get_total_quantom function.need to find out when to increment
static int total_quantums;

static struct sigaction sa;
static struct itimerval timer, remain_timer;

/**
 * freeing all the program resources, and returning the timer values to def
 */
void free_resources() {
    for (auto &it : threads_map) {
        delete it.second;
    }
    running = nullptr;
    priority_array.clear();
    threads_map.clear();
    blocked.clear();
    ready.clear();
    ids = std::priority_queue<int, std::vector<int>, std::greater<int>>();
    timer.it_value.tv_sec = 0;// first time interval, seconds part
    timer.it_value.tv_usec = 0;// first time interval, microseconds part
    timer.it_interval.tv_sec = 0;    // following time intervals, seconds part
    timer.it_interval.tv_usec = 0;    // following time intervals, microseconds part

    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr) == ERR) {
        std::cerr << SYSTEM_ERR << "bad timer setup" << std::endl;
        exit(EXIT_FAILURE);
    }
}

/**
 * @return the non terminated thread in the program
 */
int total_threads() {
    return (int) (ready.size() + blocked.size() + RUNNING_SIZE);
}

/**
 * the function initialize the program priority values
 * @param quantum_usecs
 * @param size
 * @return
 */
int init_priority_array(const int *quantum_usecs, int size) {
    if (size < MIN_SIZE) {
        return ERR;
    }
    priority_array.reserve(size);

    for (int i = 0; i < size; ++i) {
        if (quantum_usecs[i] <= 0) {
            priority_array.clear();
            return ERR;
        }
        priority_array.push_back(quantum_usecs[i]);
    }
    return EXIT_SUCCESS;
}

/**
 * the function start the timer
 * @param which how we want to modify the timer
 */
void run_timer(int which) {
    if (which == REAL_TIMER) {
        int t_priority = running->get_priority();
        timer.it_value.tv_sec = (int) priority_array[t_priority] / MICRO_TO_SEC;// first time interval, seconds part
        timer.it_value.tv_usec =
                (int) priority_array[t_priority] % MICRO_TO_SEC;// first time interval, microseconds part
        timer.it_interval.tv_sec = 0;    // following time intervals, seconds part
        timer.it_interval.tv_usec = 0;    // following time intervals, microseconds part
    } else if (which == REMAIN_TIMER) {
        timer.it_value.tv_sec = remain_timer.it_value.tv_sec;
        timer.it_value.tv_usec = remain_timer.it_value.tv_usec;
        timer.it_interval.tv_sec = remain_timer.it_interval.tv_sec;
        timer.it_interval.tv_usec = remain_timer.it_interval.tv_usec;
    } else if (which == RESET_TIMER) {
        getitimer(ITIMER_VIRTUAL, &remain_timer);
        timer.it_value.tv_sec = 0;
        timer.it_value.tv_usec = 0;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
    }
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr) == ERR) {
        std::cerr << SYSTEM_ERR << "bad timer setup" << std::endl;
        free_resources();
        exit(EXIT_FAILURE);
    }
}

/**
 * handle blocking and unblocking of signals
 * @param which the op we want to do
 */
void handle_masking(int which) {
    if (which == SET_MASK) {
        if (sigprocmask(SIG_BLOCK, &sa.sa_mask, NULL) == ERR) {
            std::cerr << SYSTEM_ERR << "bad sigprocmask setup" << std::endl;
            free_resources();
            exit(EXIT_FAILURE);
        }
        run_timer(RESET_TIMER);
    } else {
        run_timer(REMAIN_TIMER);
        if (sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL) == ERR) {
            std::cerr << SYSTEM_ERR << "bad sigprocmask setup" << std::endl;
            free_resources();
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * get the next thread from the ready ds
 * @return the next thread from the ready ds
 */
Thread *next_thread_from_ready() {
    if (!ready.empty()) {
        auto next_thread = ready.front();
        ready.pop_front();
        return next_thread;
    }
    return nullptr;
}

/**
 * set a thread to a running state
 */
void run_thread() {
    running->set_state(RUNNING);
    running->increment_quantums();
    total_quantums++;
    run_timer(REAL_TIMER);

    running->jump_env();

}

/**
 * handle the switching
 */
void alarm_switch() {
    Thread *next_runner = next_thread_from_ready();
    if (next_runner != nullptr) {
        if (running->save_env() != AFTER_JMP) {
            Thread *former_runner = running;
            former_runner->set_state(READY);
            ready.push_back(former_runner);

            running = next_runner;
            run_thread();
        }
    } else {
        running->increment_quantums();
        total_quantums++;
        run_timer(REAL_TIMER);
    }
}

/**
 * responsible on doing the switch if i try to block myself
 */
void self_block_switch() {
    //main is in ready for sure

    if (running->save_env() != AFTER_JMP) {
        Thread *next_runner = next_thread_from_ready();
        Thread *former_runner = running;

        former_runner->set_state(BLOCKED);
        blocked.push_back(former_runner);

        running = next_runner;
        sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL);
        run_thread();
    }
}

/**
 * handle the Thread switching if i try to terminate myself
 */
void self_terminate_switch() {
    //always should be in this case the main thread in ready,because it can't be blocked nor terminated and
    //get to this funcion
    Thread *next_runner = next_thread_from_ready();
    running = next_runner;
    sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL);
    run_thread();
}

/**
 * the program scheduler defined by round robin
 * @param event the operation that happened in the program
 */
void scheduler(int event) {
    switch (event) {
        case ALARM:
            alarm_switch();
            break;
        case SELF_TERMINATE:
            self_terminate_switch();
            break;
            //SELF_BLOCK
        default:
            self_block_switch();
    }

}

/**
 * init the sa
 * @param sig the main sig
 */
void timer_handler(int sig) {
    scheduler(ALARM);
}

/**
 * init the id array and the sa
 */
static void init_static_components() {
    //initializing available ids
    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        ids.push(i);
    }

    sa.sa_handler = timer_handler;

    if (sigemptyset(&sa.sa_mask) == ERR || sigaddset(&sa.sa_mask, SIGVTALRM) == ERR ||
        sigaction(SIGVTALRM, &sa, nullptr) == ERR) {
        std::cerr << SYSTEM_ERR << "bad sigaction init" << std::endl;
        exit(EXIT_FAILURE);
    }
}

/**
 * @return first available id
 */
int get_id() {
    int id = ids.top();
    ids.pop();
    return id;
}

// ~~~~~~~~~~~~~~~~~~~~~ Main program functions ~~~~~~~~~~~~~~~~~~~~~ //

/**
 * This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once.
 * @param quantum_usecs an array of the length of a quantum in micro-seconds for each priority.
 * @param size is the size of the array.
 * @return On success, return 0. On failure, return -1.
 */
int uthread_init(int *quantum_usecs, int size) {
    int is_valid = init_priority_array(quantum_usecs, size);
    if (is_valid == ERR) {
        priority_array.clear();
        std::cerr << LIB_ERR << "library init failed" << std::endl;
        return ERR;
    }

    init_static_components();

    auto main_thread = new Thread(MAIN_THREAD_ID, 0, nullptr);
    threads_map[0] = main_thread;
    main_thread->set_state(RUNNING);
    main_thread->increment_quantums();
    total_quantums++;
    running = main_thread;

    run_timer(REAL_TIMER);

    return EXIT_SUCCESS;
}

/**
 *  The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * @param f thread entry point
 * @param priority  The priority of the new thread.
 * @return On success, return the ID of the created thread.
 * On failure, return -1.
 */
int uthread_spawn(void (*f)(void), int priority) {
    if (total_threads() >= MAX_THREAD_NUM) {
        std::cerr << LIB_ERR << "library reached maximum capacity" << std::endl;
        return ERR;
    }
    if (priority < 0 || priority >= priority_array.size()) {
        std::cerr << LIB_ERR << "priority invalid" << std::endl;
        return ERR;
    }

    handle_masking(SET_MASK);
    int tid = get_id();
    auto *new_thread = new(std::nothrow) Thread(tid, priority, f);
    if (new_thread == nullptr) {
        std::cerr << SYSTEM_ERR << "bad alloc" << std::endl;
        free_resources();
        exit(EXIT_FAILURE);
    }
    ready.push_back(new_thread);
    threads_map[tid] = new_thread;
    handle_masking(RELEASE_MASK);
    return tid;
}

/**
 * This function changes the priority of the thread with ID tid.
 * If this is the current running thread, the effect should take place only the
 * next time the thread gets scheduled.
 * @param tid the thread id
 * @param priority the new priority
 * @return  On success, return 0. On failure, return -1.
 */
int uthread_change_priority(int tid, int priority) {
    if (0 <= priority && priority < (int) priority_array.size()) {
        if (threads_map.find(tid) != threads_map.end()) {
            threads_map[tid]->set_priority(priority);
            return EXIT_SUCCESS;
        }
    }
    std::cerr << LIB_ERR << "tid or priority not valid" << std::endl;
    return ERR;
}


/**
 * remove a thread from all ds -- helper for terminate
 * @param tid the thread id we want to remove
 */
void remove_thread(int tid) {
    //function execution under masking! masking release by caller!
    ids.push(tid);
    auto thread_it = threads_map.find(tid);
    switch (thread_it->second->get_state()) {
        case BLOCKED:
            for (auto it = blocked.begin(); it != blocked.end(); ++it) {
                if (*it == thread_it->second) {
                    blocked.erase(it);
                    delete threads_map[tid];
                    threads_map.erase(tid);
                    break;
                }
            }
            break;
        case READY:
            for (auto it = ready.begin(); it != ready.end(); ++it) {
                if (*it == thread_it->second) {
                    ready.erase(it);
                    delete threads_map[tid];
                    threads_map.erase(tid);
                    break;
                }
            }
            break;
            //case:READY
        default:
            delete threads_map[tid];
            running = nullptr;
            threads_map.erase(tid);
    }
}

/**
 * This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0)
 * @param tid the thread id
 * @return The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
 */
int uthread_terminate(int tid) {
    auto it = threads_map.find(tid);
    if (it != threads_map.end()) {
        handle_masking(SET_MASK);
        if (tid == MAIN_THREAD_ID) {
            free_resources();
            //don't need release masking!
            exit(EXIT_SUCCESS);
        } else if (tid == running->get_id()) {
            remove_thread(tid);
            //scheduler releases masking!
            scheduler(SELF_TERMINATE);
        } else {
            remove_thread(tid);
            handle_masking(RELEASE_MASK);
            return EXIT_SUCCESS;
        }
    }
    std::cerr << LIB_ERR << "couldn't terminate " << tid << ".invalid tid" << std::endl;
    return ERR;
}

/**
 * This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * @param tid the Thread id
 * @return  On success, return 0. On failure, return -1.
 */
int uthread_block(int tid) {
    auto map_iterator = threads_map.find(tid);
    if (map_iterator != threads_map.end()) {
        if (tid == MAIN_THREAD_ID) {
            std::cerr << LIB_ERR << "couldn't block " << tid << "this is the main thread!" << std::endl;
            return ERR;
        }

        handle_masking(SET_MASK);
        if (tid == running->get_id()) {
            scheduler(SELF_BLOCK);
        } else if (map_iterator->second->get_state() != BLOCKED) {
            for (auto ready_iterator = ready.begin(); ready_iterator != ready.end(); ++ready_iterator) {
                if (*ready_iterator == map_iterator->second) {
                    ready.erase(ready_iterator);
                    threads_map[tid]->set_state(BLOCKED);
                    blocked.push_back(map_iterator->second);
                    break;
                }
            }

        }
        handle_masking(RELEASE_MASK);
        return EXIT_SUCCESS;
    }
    std::cerr << LIB_ERR << "couldn't block " << tid << ".invalid tid" << std::endl;
    return ERR;
}

/**
 * This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * @param tid the Thread id
 * @return  On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid) {
    auto it = threads_map.find(tid);
    if (it != threads_map.end()) {
        if (it->second->get_state() == BLOCKED) {
            handle_masking(SET_MASK);
            for (auto rit = blocked.begin(); rit != blocked.end(); ++rit) {
                if (*rit == it->second) {
                    blocked.erase(rit);
                    it->second->set_state(READY);
                    ready.push_back(it->second);
                    break;
                }
            }
            handle_masking(RELEASE_MASK);
        }
        return EXIT_SUCCESS;
    }
    std::cerr << LIB_ERR << "couldn't resume " << tid << ".invalid tid" << std::endl;
    return ERR;
}

/**
 *  This function returns the thread ID of the calling thread.
 * @return  The ID of the calling thread.
 */
int uthread_get_tid() {
    return running->get_id();
}

/**
 *  This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * @return The total number of quantums.
 */
int uthread_get_total_quantums() {
    return total_quantums;
}

/**
 * This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * @param tid the Thread id
 * @return On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
 */
int uthread_get_quantums(int tid) {

    if (threads_map.find(tid) != threads_map.end()) {
        return threads_map[tid]->get_quantums();
    }
    std::cerr << LIB_ERR << "couldn't get quantum of " << tid << ".invalid tid" << std::endl;

    return ERR;
}

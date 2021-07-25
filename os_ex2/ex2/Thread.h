#ifndef THREAD_H
#define THREAD_H

#include <setjmp.h>
#include <signal.h>

#define RUNNING 3
#define BLOCKED 2
#define READY 1
#define STACK_SIZE 4096
#define AFTER_JMP 1
#define SAVE_MASK 1

class Thread {
private:
    int _tid, _priority, _state, _quantums;
    char *_t_stack;

    void (*_func)();

    sigjmp_buf _env;

public:
    /**
     *
     * Thread constructor
     * @param tid the thread id
     * @param priority the thread priority
     * @param func the thready starting point
     */
    Thread(int tid, int priority, void (*func)());

    /**
     * Thread destructor
     */
    ~Thread() {
        delete[] _t_stack;
    }

    /**
     * getter
     * @return the thread id
     */
    int get_id() const {

        return _tid;
    }

    /**
     * getter
     * @return the thread priority
     */
    int get_priority() const {
        return _priority;
    }

    /**
     * getter
     * @return the thread state
     */
    int get_state() const {
        return _state;
    }

    /**
     * getter
     * @return the thread quantums
    */
    int get_quantums() {
        return _quantums;
    }

    /**
    * setter for the priority field
    * @param new_priority the new priority
    */
    void set_priority(int new_priority) {
        _priority = new_priority;
    }

    /**
     * setter for the state field
     * @param new_state the new state
     */
    void set_state(int new_state) {
        _state = new_state;
    }

    /**
     * increment the quantums by 1
     */
    void increment_quantums() {
        _quantums++;
    }

    /**
     * save the thread environment
     * @return sigsetjmp output
     */
    int save_env();

    /**
       * jump to other thread using siglongjmp
       */
    void jump_env() {
        siglongjmp(_env, AFTER_JMP);
    }
};


#endif //OS_EX2LOCAL_THREAD_H

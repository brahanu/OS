#include "Thread.h"


#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
        "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif

/**
 *
 * Thread constructor
 * @param tid the thread id
 * @param priority the thread priority
 * @param func the thready starting point
 */
Thread::Thread(int tid, int priority, void (*func)(void)) : _tid(tid), _priority(priority), _func(func), _state(READY),
                                                            _quantums(0) {
    _t_stack = new char[STACK_SIZE];
    address_t sp, pc;

    sp = (address_t) _t_stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t) _func;
    sigsetjmp(_env, SAVE_MASK);
    if (func != nullptr) {
        (_env->__jmpbuf)[JB_SP] = translate_address(sp);

        (_env->__jmpbuf)[JB_PC] = translate_address(pc);
    }
    sigemptyset(&_env->__saved_mask);
}

/**
 * save the thread environment
 * @return sigsetjmp output
 */
int Thread::save_env() {
    return sigsetjmp(_env, SAVE_MASK);
}


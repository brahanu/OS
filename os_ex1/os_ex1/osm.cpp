#include <sys/time.h>
#include "osm.h"

#define ERR -1
#define NOT_VALID 0
#define UNROLL_FACTOR 5
struct timeval start, end;

unsigned int loop_unrolling_factor(const unsigned int &iterations) {
    unsigned int real_iterations = iterations;
    unsigned int reminder = iterations % UNROLL_FACTOR;
    if (reminder != 0) {
        real_iterations += reminder; // round factor
    }
    return real_iterations;
}

double calculate_time_taken() {
    double time_taken = (end.tv_sec - start.tv_sec) * 1e6;
    return (double) (time_taken + (end.tv_usec - start.tv_usec)) * 1e3;
}

double osm_operation_time(unsigned int iterations) {
    if (iterations == NOT_VALID) {
        return ERR;
    } else {
        int a = 0, b = 0, c = 0;
        unsigned int real_iterations = loop_unrolling_factor(iterations);
        int is_valid = gettimeofday(&start, nullptr);
        if (is_valid != ERR) {
            for (unsigned int i = 0; i < real_iterations; i = i + UNROLL_FACTOR) {
                a = a + 1; //loop unrolling , factor 5
                b = b + 1;
                c = c + 1;
                a = a + 1;
                b = b + 1;
            }
            is_valid = gettimeofday(&end, nullptr);
            if (is_valid != ERR) {
                return calculate_time_taken() / (double) (real_iterations);
            }
        }

        return ERR;
    }
}

void empty_function() {}

double osm_function_time(unsigned int iterations) {
    if (iterations == NOT_VALID) {
        return ERR;
    } else {
        unsigned int real_iterations = loop_unrolling_factor(iterations);
        int is_valid = gettimeofday(&start, nullptr);
        if (is_valid != ERR) {
            for (unsigned int i = 0; i < real_iterations; i = i + UNROLL_FACTOR) {
                empty_function();
                empty_function();
                empty_function();
                empty_function();
                empty_function();
            }
            is_valid = gettimeofday(&end, nullptr);
            if (is_valid != ERR) {
                return calculate_time_taken() / (double) (real_iterations);
            }
        }

        return ERR;
    }
}

double osm_syscall_time(unsigned int iterations) {
    if (iterations == NOT_VALID) {
        return ERR;
    } else {
        unsigned int real_iterations = loop_unrolling_factor(iterations);
        int is_valid = gettimeofday(&start, nullptr);
        if (is_valid != ERR) {
            for (unsigned int i = 0; i < real_iterations; i = i + UNROLL_FACTOR) {
                OSM_NULLSYSCALL;
                OSM_NULLSYSCALL;
                OSM_NULLSYSCALL;
                OSM_NULLSYSCALL;
                OSM_NULLSYSCALL;
            }
            is_valid = gettimeofday(&end, nullptr);
            if (is_valid != ERR) {
                return calculate_time_taken() / (double) (real_iterations);
            }
        }
        return ERR;
    }
}

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


#define DECREASING_CASE 0

#if DECREASING_CASE
static int burst_table_dec[5] = { 5, 4, 3, 2, 1 };
#else
static int burst_table_inc[5] = { 1, 2, 3, 4, 5 };
#endif

static int
get_burst_for_child(int child_id)
{
    // child_id is 1..5
    if (child_id < 1) child_id = 1;
    if (child_id > 5) child_id = 5;

#if DECREASING_CASE
    return burst_table_dec[child_id - 1];
#else
    return burst_table_inc[child_id - 1];
#endif
}


// Dummy calculation function to simulate CPU burst
void cpu_burst(int iterations) {
    
    int start = uptime();        // ticks since boot
    while (uptime() - start < iterations*10) {
            // busy wait: burn CPU
    }
}


void child_process(int child_id) {
    int burst_input;
    int base = get_burst_for_child(child_id);
    
   
    int j;
    for (j = 0; j < 3; j++) {
        burst_input = 1+getpid();
        burst_input = burst_input * base;
        cpu_burst(burst_input);
        
    }
}

int main(void) {
    int i;
    
    for (i = 0; i < 5; i++) {
        int pid = fork();
        
        if (pid < 0) {
            printf("Fork failed for child %d\n", i);
            exit(1);
        } else if (pid == 0) {
            
            child_process(i + 1);
            exit(0);  
        } else {
            
            printf("Parent: Forked child %d with PID %d\n", i + 1, pid);
        }
    }
    
    
   
    
    for (i = 0; i < 5; i++) {
        wait(0);
    }
    
    
    exit(0);
}

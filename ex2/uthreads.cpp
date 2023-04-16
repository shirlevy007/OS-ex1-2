#include "uthreads.h"
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
#include <csetjmp>
#include <signal.h>
#include <sys/time.h>


//------------------------------------------------- enum ThreadState -------------------------------------------------//
typedef enum ThreadState {
  READY, RUNNING, BLOCKED
} ThreadState;


//-------------------------------------------------- struct Thread ---------------------------------------------------//
/**
 * @brief: thread struct - holds thread related variables
 */
struct Thread {
    #define JB_SP 6
    #define JB_PC 7
    typedef unsigned long address_t;

    /**
     * A translation is required when using an address of a variable.
     * Use this as a black box in your code.
     */
    address_t translate_address(address_t addr) {
        address_t ret;
        asm volatile("xor    %%fs:0x30,%0\n"
                     "rol    $0x11,%0\n"
                     : "=g" (ret)
                     : "0" (addr));
        return ret;
    }

    /**
     * initializes env to use the right stack, and to run from the function 'entry_point', when we'll use
     * siglongjmp to jump into the thread.
     */
    void setup_thread(thread_entry_point entry_point) {
        address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry_point;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&(env->__saved_mask));
    }

    /**
     * Fields
     */
    int tid;
    int quantums;
    int sleep_quantums;
    ThreadState state;
    char* stack;
    sigjmp_buf env;

    /**
    * constructor
    */
    Thread(int tid, thread_entry_point entry_point) {
        this->tid = tid;
        this->quantums = 0;
        this->sleep_quantums = 0;
        this->state = READY;
        this->stack = new char[STACK_SIZE];
        setup_thread(entry_point);
    }

    ~Thread() { delete[] stack;  }
};


//---------------------------------------------------- CONSTANTS -----------------------------------------------------//
#define BLOCK_SIG_FUNC sigprocmask(SIG_BLOCK, &sig_set, nullptr)
#define ALLOW_SIG_FUNC sigprocmask(SIG_UNBLOCK, &sig_set, nullptr)
#define FAILURE (-1)
#define INVALID_TID_ERROR "Invalid tid."
#define INVALID_QUANTUMS "quantums_usecs must be > 0"
#define BLOCK_THREAD (SIGVTALRM + 1)
#define TERMINATE_THREAD (SIGVTALRM + 2)
#define SLEEP_THREAD (SIGVTALRM + 3)

//--------------------------------------------------- GLOBAL VARS ----------------------------------------------------//
int library_quantums = 0;
int thread_counter = 0;

Thread* current_running;

Thread* threads[MAX_THREAD_NUM];
std::vector<Thread*> ready_queue;
std::set<Thread*> sleeping_set;

struct sigaction scheduler_sa = {nullptr};
struct itimerval scheduler_timer{};

void next_running(int next_tid);

sigset_t sig_set;

//------------------------------------------------ Helper Functions --------------------------------------------------//
/**
 * @brief: updates the number of each quantums still needs to sleep each quantum that passes
 */
void update_sleeping_set() {
    std::set<Thread*> new_set;
    for(auto thread: sleeping_set) {
        if(thread->sleep_quantums == 0) {
            if(thread->state == READY) {
                ready_queue.push_back(thread);
            }
        } else { //who stays in
            new_set.insert(thread);
        }
        if(thread->sleep_quantums > 0){
            thread->sleep_quantums--;
        }
    }
    sleeping_set = new_set;
}

/**
* @brief: creates a new thread and adds it to ready
 */
void thread_creator(int tid = 0, thread_entry_point entry_point = nullptr) {
  threads[tid] = new Thread(tid, entry_point);
  if (tid == 0) {
    threads[tid]->state = RUNNING;
  } else {
    ready_queue.push_back(threads[tid]);
  }
  thread_counter++;
}

bool invalid_tid(int tid) {
  return tid < 0 || tid >= MAX_THREAD_NUM || threads[tid] == nullptr;
}

/**
 * @brief: removes a thread from the ready queue
 */
void pop_out_of_ready_queue(Thread *thread_to_pop) {
  auto iter = std::find(ready_queue.begin(), ready_queue.end(), thread_to_pop);
  if (iter != ready_queue.end()) {
    ready_queue.erase(iter);
  } else {
    std::cerr << "Thread was not in ready_queue." << std::endl;
    exit(FAILURE);
  }
}

void next_running(int next_tid) {
    ready_queue.erase(ready_queue.begin());
    current_running = threads[next_tid];
    current_running->state = RUNNING;
    current_running->quantums++;
    library_quantums++;
}

/**
 * @brief Saves the current thread state, and jumps to the other thread.
 */
void switch_threads(int prev_tid, int next_tid) {
    int ret_val = sigsetjmp(threads[prev_tid]->env, 1);
    bool did_just_save_bookmark = (ret_val == 0);
    if (did_just_save_bookmark) {
        siglongjmp(threads[next_tid]->env, 1);
    }
}

/**
 * @brief engine of everything. deals with different signals and actions to switch the running thread and update
 * various data structures.
 */
void scheduler(int sig) {
    BLOCK_SIG_FUNC;
    // update all the sleeping threads
    update_sleeping_set();
    // reset the quantum time expiration
    if (setitimer(ITIMER_VIRTUAL, &scheduler_timer, nullptr)) {
        std::cerr << "setitimer failed" << std::endl;
    }

    switch (sig) {
        case SIGVTALRM: {
            if (!ready_queue.empty()) {
                // pop first element from queue and define as running
                int prev_tid = current_running->tid, next_tid = ready_queue[0]->tid;
                next_running(next_tid);

                // insert prev running thread to the end of ready_queue
                ready_queue.push_back(threads[prev_tid]);
                threads[prev_tid]->state = READY;

                // switch the envs:
                ALLOW_SIG_FUNC;
                switch_threads(prev_tid, next_tid);
            }
            break;
        }

        case BLOCK_THREAD: {
            // pop first element from queue and define as running
            int prev_tid = current_running->tid, next_tid = ready_queue[0]->tid;
            next_running(next_tid);

            // switch the envs:
            ALLOW_SIG_FUNC;
            switch_threads(prev_tid, next_tid);
            break;
        }

        case SLEEP_THREAD: {
            int prev_tid = current_running->tid, next_tid = ready_queue[0]->tid;
            next_running(next_tid);

            // switch the envs:
            ALLOW_SIG_FUNC;
            switch_threads(prev_tid, next_tid);
            break;
        }

        case TERMINATE_THREAD: {
            int next_tid = ready_queue[0]->tid;
            next_running(next_tid);

            // switch the envs:
            ALLOW_SIG_FUNC;
            siglongjmp(threads[next_tid]->env, 1);
        }
    }
}


//------------------------------------------------- Threads Manager --------------------------------------------------//
/**
 * @brief initializes the thread library.
 *
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    if(quantum_usecs <= 0) {
      std::cerr << INVALID_QUANTUMS << std::endl;
      return FAILURE;
    }

    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGVTALRM);

    // Install timer_handler as the signal handler for SIGVTALRM.
    scheduler_sa.sa_handler = &scheduler;
    if (sigaction(SIGVTALRM, &scheduler_sa, nullptr) < 0) {
      std::cerr << "sigaction failed." << std::endl;
      return FAILURE;
    }

    // Configure the scheduler_timer to expire after quantum_usecs... */
    scheduler_timer.it_value.tv_sec = 0;                     // first time interval, seconds part
    scheduler_timer.it_value.tv_usec = quantum_usecs;        // first time interval, microseconds part

    // configure the scheduler_timer to expire every quantum_usecs after that.
    scheduler_timer.it_interval.tv_sec = 0;                 // following time intervals, seconds part
    scheduler_timer.it_interval.tv_usec = quantum_usecs;    // following time intervals, microseconds part

    // Start a virtual scheduler_timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &scheduler_timer, nullptr)) {
      std::cerr << "setitimer failed" << std::endl;
      return FAILURE;
    }

    thread_creator();
    current_running = threads[0];

    current_running->quantums++;
    library_quantums++;

    return EXIT_SUCCESS;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point){
    if(thread_counter == MAX_THREAD_NUM) {
        std::cerr << "Can't have more than MAX_THREAD_NUM" << std::endl;
        return FAILURE;
    }
    if( entry_point == nullptr) {
        std::cerr << "entry_point mustn't be null." << std::endl;
        return FAILURE;
    }

    BLOCK_SIG_FUNC;
    int tid = 0;
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (threads[i] == nullptr) {
            tid = i;
            break;
        }
    }

    if (tid == 0) {
        std::cerr << "Cannot find a free place in threads array." << std::endl;
        ALLOW_SIG_FUNC;
        return FAILURE;
    }

    thread_creator(tid, entry_point);
    ALLOW_SIG_FUNC;
    return tid;
}
/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid) {
  if (invalid_tid(tid)) {
      std::cerr << INVALID_TID_ERROR << "cant terminate tid: "<< tid << std::endl;
    return FAILURE;
  }

  BLOCK_SIG_FUNC;
  if (tid == 0) {
//      ready_queue.clear();
      for (int i=0; i<MAX_THREAD_NUM; i++) {
          // delete all the threads object
          if (threads[i] != nullptr) {
              delete threads[i];
              threads[i] = nullptr;
          }
      }
      exit(EXIT_SUCCESS);
  }

  // terminate itself (the running thread)
  if (tid == current_running->tid) { // kill itself and call scheduler
      if (threads[tid] != nullptr) {
          delete threads[tid];
          threads[tid] = nullptr;
      }
      thread_counter--;
      scheduler(TERMINATE_THREAD);
  }

  // terminate a ready thread
  else if (threads[tid]->state == READY) {
    pop_out_of_ready_queue(threads[tid]);
    if (threads[tid] != nullptr) {
        delete threads[tid];
        threads[tid] = nullptr;
    }
    thread_counter--;
  }

  // terminate a block thread
  else {
      if (threads[tid] != nullptr) {
          delete threads[tid];
          threads[tid] = nullptr;
      }
    thread_counter--;
  }
  ALLOW_SIG_FUNC;
  return EXIT_SUCCESS;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid){
  if (invalid_tid(tid) || tid==0) {
    std::cerr << INVALID_TID_ERROR << "cant block tid: "<< tid << std::endl;
    return FAILURE;
  }

  BLOCK_SIG_FUNC;
  // block itself and call scheduler
  if (tid == current_running->tid) {
    threads[tid]->state = BLOCKED;
    scheduler(BLOCK_THREAD);
  }

  // block a ready thread
  else if (sleeping_set.count(threads[tid]) == 1) {
      threads[tid]->state = BLOCKED;
  }

  // block a ready thread
  else if (threads[tid]->state == READY) {
    pop_out_of_ready_queue(threads[tid]);
    threads[tid]->state = BLOCKED;
  }


  ALLOW_SIG_FUNC;
  return EXIT_SUCCESS;
}


/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {
  if (invalid_tid(tid)) {
      std::cerr << INVALID_TID_ERROR <<"cant resume tid: "<< tid << std::endl;
    return FAILURE;
  }

  BLOCK_SIG_FUNC;
  if(threads[tid]->state == BLOCKED) {
      threads[tid]->state = READY;
      if (threads[tid]->sleep_quantums == 0) {
          ready_queue.push_back(threads[tid]);
      }
  }
  ALLOW_SIG_FUNC;
  return EXIT_SUCCESS;
}


/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY threads list.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid==0) calls this function.

 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums) {
  if (current_running->tid == 0) {
    std::cerr << INVALID_TID_ERROR << "can't sleep" << std::endl;
    return FAILURE;
  }
  BLOCK_SIG_FUNC;

  current_running->sleep_quantums = num_quantums;
  current_running->state = READY;
  sleeping_set.insert(current_running);
  scheduler(SLEEP_THREAD);

  ALLOW_SIG_FUNC;
  return EXIT_SUCCESS;
}


/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid() {
     if (current_running == nullptr) {
       std::cerr << "Invalid running thread" <<std::endl;
       return FAILURE;
     }
     return current_running->tid;
}


/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums() {
    return library_quantums;
}


/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid) {
  if (invalid_tid(tid)) {
      std::cerr << "cant get_quantums for tid: "<< tid<< std::endl;
    return FAILURE;
  }
  return threads[tid]->quantums;
}



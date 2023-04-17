
#include <stdio.h>
#include <signal.h>
#include <csetjmp>
#include <uthreads.h>
#include <deque>
#include <csignal>
#include <unistd.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <iostream>

//#define MAX_THREAD_NUM 100 /* maximal number of threads */
//#define STACK_SIZE 4096 /* stack size per thread (in bytes) */
//#define JB_SP 6
//#define JB_PC 7
//typedef void (*thread_entry_point)(void);
typedef unsigned long int address_t;

///--------------------------- system errors -----------------------------

#define INVALID_ALLOC ("system error: memory allocation failed")
#define SIGACTION_FAILED ("system error: sigaction error")
#define SET_TIMER_FAILED ("system error: set timer error")


///--------------------------- thread library errors -----------------------------
#define INVALID_QUANTUM ("thread library error: invalid input: quantum_usecs should be positive")
#define INVALID_EP ("thread library error: invalid input: entry_point cannot be null")
#define ABOVE_MAX ("thread library error: invalid: creation of too many threads")
#define ABOVE_MAX ("thread library error: invalid: creation of too many threads")
#define NO_THREAD ("thread library error: invalid: no thread with tid as requested")
#define FAIL (-1)

enum State {
    READY, RUNNING, BLOCKED
};

enum SigCase {
    SIGSLEEP=27, SIGTERMINATE
};

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
  address_t ret;
  asm volatile("xor    %%gs:0x18,%0\n"
               "rol    $0x9,%0\n"
  : "=g" (ret)
  : "0" (addr));
  return ret;
}

//TODO: MAKE SURE ITS OK:
class Thread {

  unsigned int tid;
  unsigned int quantum;
  char *stack;
  thread_entry_point entry_point;
  State state;
  sigjmp_buf env;
  address_t sp;
  address_t pc;

 public:
//    Thread(){
//        Thread(0,0,std::nullptr_t);
//    }
    Thread(int tid, thread_entry_point entry_point) {
        this->tid = tid;
        this->entry_point = entry_point;
        this->state = READY;
        this->stack = new char[STACK_SIZE];
        valid_allocation();
    }
    bool valid_allocation(){
        if (this->stack == nullptr){
            std::cerr << INVALID_ALLOC << std::endl;
            exit(1);
        }
    }
    int setup_thread(){
      this->sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
      this->pc = (address_t) entry_point;
      sigsetjmp(env, 1);
//      (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
//      (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
      sigemptyset(&env[tid].__saved_mask);
    }
    ~Thread(){
      delete[] stack;
    }
    State get_state(){
        return this->state;
    }
    unsigned int get_tid(){
        return this->tid;
    }

};

///------------------------------------------- Global vars-------------------------------------------------------
//int current_thread = 0; //current thread index in the threads list
int creations = 0; //0-MAX, counter of the creations of threads
Thread * threads[MAX_THREAD_NUM]; //threads list
std::deque<Thread*> ready_queue; // ready threads queue
int quantum; // the quantum for the timer
struct itimerval timer;
Thread * running_thread;


void timer_handler(int sig)
{
    switch (sig) {
        case SIGVTALRM: // timer expires
            ready_queue.push_back(running_thread);
//            running_thread = (Thread *) ready_queue.
            break;
        case SIGSLEEP: // thread blocked
            break;
        case SIGTERMINATE: // termination of thread
            break;

    }



    timer.it_value.tv_sec = quantum;        // first time interval, seconds part
    timer.it_value.tv_usec = 0;        // first time interval, microseconds part
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        std::cerr << SET_TIMER_FAILED << std::endl;
        exit(1);
    }
}

///------------------------------------------- Library-------------------------------------------------------
/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/

int uthread_init(int quantum_usecs) {
    if(quantum_usecs <= 0) {
    std::cerr << INVALID_QUANTUM << std::endl;
    return FAIL;
    }


    //TODO: understand how to initialize main thread

    quantum = quantum_usecs;
    struct sigaction sa = {0};
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        std::cerr << SIGACTION_FAILED << std::endl;
        exit(1);
    }
    // Configure the timer to expire after quantum_usecs... */
    timer.it_value.tv_sec = 0;        // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs;        // first time interval, microseconds part
    // configure the timer to expire every quantum_usecs sec after that.
    timer.it_interval.tv_sec = 0;    // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs;    // following time intervals, microseconds part

    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        std::cerr << SET_TIMER_FAILED << std::endl;
        exit(1);
    }



}

///----------------------------------------------------------------------------

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point) {
  if(entry_point == nullptr) {
    std::cerr << INVALID_EP << std::endl;
    return FAIL;
  }
  int tid = helper_spawn ();
  if(tid < 0) {
    std::cerr << ABOVE_MAX << std::endl;
    return FAIL;
  }
  // entry_point is valid, and there is an available spot in the threads list
  Thread * t = new Thread(tid, entry_point);
  threads[tid] = t; // adds to our threads list
  creations++; // adds the counter of threads in the threads list
  ready_queue.push_back(t); // adds the thread to the back of the ready queue
  //TODO: if tid==0 -> main thread should be RUNNING - here?
  return tid;
}

int helper_spawn() {
  for(int i = 0; i < MAX_THREAD_NUM; ++i) {
    if(threads[i] == nullptr) {
      return i;
    }
  }
  // no empty spot at the threads list
  return FAIL;
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
    if(tid<0 || tid>=MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << NO_THREAD << std::endl;
        return FAIL;
    }
    if(tid != 0) {
        if (threads[tid]->get_state()== RUNNING){ // a running thread terminates itself
            terminate_thread(tid);
            //TODO: from scheduler - change to next thread. see if to move forward to next thread
        }
        ///TODO: when blocked or when ready - different? remove from queue?
//        if (threads[tid]->get_state()== READY){
//            terminate_thread(tid);
//            return 0;
//        }
//        if (threads[tid]->get_state()== BLOCKED){
//            terminate_thread(tid);
//            return 0;
//        }

        terminate_thread(tid);
        return 0;
    }
    for (int i = 1; i < MAX_THREAD_NUM; ++i) {
        if(threads[i] != nullptr) {
            terminate_thread(i);
        }
    }
    terminate_thread(0);
    exit(0);
}

int terminate_thread(int tid){
    delete threads[tid];
    threads[tid] = nullptr;
    creations--;
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
int uthread_block(int tid) {
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
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums) {
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid() {
    return running_thread->get_tid();
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
}



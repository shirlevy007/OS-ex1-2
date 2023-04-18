
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
#include <set>

//#define MAX_THREAD_NUM 100 /* maximal number of threads */
//#define STACK_SIZE 4096 /* stack size per thread (in bytes) */
#define JB_SP 6
#define JB_PC 7
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
#define NO_BLOCKING_MAIN ("thread library error: blocking main thread is invalid")
#define EXIT_FAIL (-1)
#define EXIT_SUCCESS (0)

enum State {
    READY, RUNNING, BLOCKED
};

enum SigCase {
    SIGSLEEP=SIGVTALRM+1, SIGTERMINATE
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

///------------------------------------------- Thread ------------------------------------------------------

//TODO: MAKE SURE ITS OK:
class Thread {

  unsigned int tid;
  int running_quantums;
  unsigned int sleep_quantums;
  char *stack;
  thread_entry_point entry_point;
  State state;
  sigjmp_buf env[MAX_THREAD_NUM]{0};
//  address_t sp;
//  address_t pc;

 public:
    Thread() { // defoult constructor only for main thread.
        this->tid = 0; // as in instructions
        this->entry_point = nullptr; // main thread has no function
        this->state = READY;
        this->stack = new char[STACK_SIZE];
        valid_allocation();
        this->running_quantums=0;
        this->sleep_quantums=0;
        setup_thread();
    }

    Thread(int tid, thread_entry_point entry_point) {
        this->tid = tid;
        this->entry_point = entry_point;
        this->state = READY;
        this->stack = new char[STACK_SIZE];
        valid_allocation();
        this->running_quantums=0;
        this->sleep_quantums=0;
        setup_thread();
    }
    bool valid_allocation(){
        if (this->stack == nullptr){
            std::cerr << INVALID_ALLOC << std::endl;
            exit(1);
        }
    }
    int setup_thread(){
        address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry_point;
        sigsetjmp(env[tid], 1);
        (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
        (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env[tid]->__saved_mask);
    }
    ~Thread(){
      delete[] stack;
    }
    void set_state(State s){
        this->state = s;
    }
    State get_state(){
        return this->state;
    }
    void set_sleep_quantums(unsigned int sq){
        this->sleep_quantums = sq;
    }
    unsigned int decrease_sleep_quantums(){
        if (this->sleep_quantums>0){
            return --this->sleep_quantums;
        }
        return this->sleep_quantums;
    }
    unsigned int get_sleep_quantums(){
        return this->sleep_quantums;
    }
    unsigned int get_tid(){
        return this->tid;
    }
    int get_running_quantum(){
        return this->running_quantums;
    }
    void increase_running_quantum(){
        this->running_quantums++;
    }
//    sigjmp_buf get_env(){
//        return this->env;
//    }

};

///------------------------------------------- Global vars -------------------------------------------------------

//int current_thread = 0; //current thread index in the threads list
int creations = 0; //0-MAX, counter of the creations of threads
Thread * threads[MAX_THREAD_NUM]; //threads list
std::deque<Thread*> ready_queue; // ready threads queue
std::set<Thread*> sleeping; // ready threads queue
int quantum; // the quantum for the timer
struct itimerval timer;
int total_quantums=0;

//bool block_by_state(State s);

//bool block_by_state(State state, Thread *pThread);

Thread * running_thread;


///------------------------------------------- Timer -------------------------------------------------------

void check_sleep_list(){
    // checking sleeping list for unblocked thread which are sone waiting
    for (Thread * blocked: sleeping){
        if(!blocked->decrease_sleep_quantums()){ //decreases the sleep quantums by 1, return the remaining.
            // sleep_quantums=0 so done waiting
            if (blocked->get_state()!=BLOCKED){
                ready_queue.push_back(blocked); // means sleep_quantums is 0 & not blocked
                sleeping.erase(blocked); // TODO: problem to delete while running in a loop?
            }
        }
    }
}

void restart_timer(){
    total_quantums++;
//    check_sleep_list(); // checkes blocked list for whenever time expires todo: also when blocked or only when time expires?

    // Configure the timer to expire after quantum_usecs. seconds set as 0 already from global */
    timer.it_value.tv_usec = quantum;        // first time interval, microseconds part
    // configure the timer to expire every quantum_usecs sec after that.
    timer.it_interval.tv_usec = quantum;    // following time intervals, microseconds part

    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
        std::cerr << SET_TIMER_FAILED << std::endl;
        exit(1);
    }
}


void timer_handler(int sig)
{
    switch (sig) {
        case SIGVTALRM: // timer expires
            ready_queue.push_back(running_thread); // move the last running thread to end of ready list
            running_thread->set_state(READY);

            // checking sleeping list
            check_sleep_list();

            break;

        case SIGSLEEP: // thread blocked
            sleeping.insert(running_thread); //TODO: make sure there is sleeping quantum and\or blocked state
//            restart_timer();
            break;

        case SIGTERMINATE: // termination of thread

//            restart_timer();
            break;

    }
    //prev running thread is last at ready_queue\ sleeping set\ terminated.
    running_thread = ready_queue.front(); // move the first thread in ready line up to running state
    running_thread->set_state(RUNNING);
    running_thread->increase_running_quantum();
    ready_queue.pop_front();
    restart_timer();
}

/**
 *
 * @param s state of the thread meant to be blocked
 * @param thread_to_block  a pointer to the thread meant to be blocked
 * @return true if blocking was successful, otherwise false
 */
bool block_by_state(State s, Thread *thread_to_block) {
    switch (s) {
        case BLOCKED:
            return true;

        case RUNNING:
//            if(thread_to_block == running_thread){
//                //make sure no RUNNING thread except running_thread
//                /////////////////////////////////////////////////// todo:make sure no need for this check in the end
//                std::cerr << "more than one thread running" << std::endl;
//                return FAIL;
//            }
//            running_thread->set_state(BLOCKED);
//            timer_handler(SIGSLEEP);
            ///a scheduling decision should be made
            //TODO: scheduling decision

            break;

        case READY:
            int index_in_queue;
            // find the index of the thread we want to block in the ready_queue
            for (int i = 0; i < ready_queue.size(); ++i) {
                if (ready_queue.at(i)->get_tid()==thread_to_block->get_tid()){
                    index_in_queue = i;
                    break;
                }
            }
            thread_to_block->set_state(BLOCKED); // set state to blocked
            sleeping.insert(thread_to_block); // add to sleeping set
            ready_queue.erase(ready_queue.begin()+index_in_queue); // erase the thread from ready list
            break;
    }
    return false;
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
    return EXIT_FAIL;
    }


    Thread * main = new Thread();
    //TODO: understand how to initialize main thread

    quantum = quantum_usecs;
    struct sigaction sa = {0};
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        std::cerr << SIGACTION_FAILED << std::endl; // sigaction failed
        exit(1);
    }
    restart_timer();
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
    return EXIT_FAIL;
  }
  int tid = helper_spawn ();
  if(tid < 0) {
    std::cerr << ABOVE_MAX << std::endl;
    return EXIT_FAIL;
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
  return EXIT_FAIL;
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
        return EXIT_FAIL;
    }
    if(tid != 0) {
        if (threads[tid]->get_state()== RUNNING){ // a running thread terminates itself
            terminate_thread(tid);
            //TODO: from scheduler - change to next thread. see if to move forward to next thread
        }
        ///TODO: when blocked or when ready - different? remove from queue?
//        if (threads[tid]->get_state()== READY){
//            terminate_thread(tid);
//            return EXIT_SUCCESS;
//        }
//        if (threads[tid]->get_state()== BLOCKED){
//            terminate_thread(tid);
//            return EXIT_SUCCESS;
//        }

        terminate_thread(tid);
        return EXIT_SUCCESS;
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
    if(tid<0 || tid>=MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << NO_THREAD << std::endl;
        return EXIT_FAIL;
    }
    if(tid==0) {
        std::cerr << NO_BLOCKING_MAIN << std::endl;
        return EXIT_FAIL;
    }
    //blocks the thread according to instructions by it's state.
    if (block_by_state(threads[tid]->get_state(), threads[tid])){
        return EXIT_SUCCESS;
    }
    return EXIT_FAIL;

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
    if(tid<0 || tid>=MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << NO_THREAD << std::endl;
        return EXIT_FAIL;
    }
    if(threads[tid]->get_state()==BLOCKED){
        threads[tid]->set_state(READY); //still sleeping until next experation of timer
    }
    return EXIT_SUCCESS;
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

    running_thread->set_state(BLOCKED);
    // TODO: a scheduling decision should be made.


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
    // the total_quantums increases by 1 whenever restart_timer is called (each time a new quantum starts)
    return total_quantums;
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
    if(tid<0 || tid>=MAX_THREAD_NUM || threads[tid] == nullptr) {
        std::cerr << NO_THREAD << std::endl;
        return EXIT_FAIL;
    }
    return threads[tid]->get_running_quantum();
}



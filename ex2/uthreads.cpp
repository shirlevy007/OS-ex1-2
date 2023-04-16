#define MAX_THREAD_NUM 100 /* maximal number of threads */
#define STACK_SIZE 4096 /* stack size per thread (in bytes) */

#define INVALID_QUANTUM ("invalid input: quantum_usecs should be positive")
#define INVALID_EP ("invalid input: entry_point cannot be null")
#define ABOVE_MAX ("invalid: creation of too many threads")
#define FAIL(-1)

enum States
{
    READY, RUNNING, BLOCKED
};

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

//TODO: MAKE SURE ITS OK:

class Tread
{
#define JB_SP 6
#define JB_PC 7
  typedef unsigned long int address_t;

  unsigned int tid;
  unsigned int quantum;
  char *stack;
  States state;
  sigjmp_buf env;
  address_t sp;
  address_t pc;

 public:
  Thread(unsigned int tid, void *stack, thread_entry_point entry_point)
  {
    this.tid = thread_count++;
    address_t sp = (address_t) stack + STACK_SIZE - sizeof (address_t);
    address_t pc = (address_t) entry_point;
    sigsetjmp (env, thread_count);
    (env->__jmpbuf)[JB_SP] = translate_address (sp);
    (env->__jmpbuf)[JB_PC] = translate_address (pc);
    sigemptyset (&env->__saved_mask);
  }
//
//  void start()
//  {
//    siglongjmp (env, thread_count++);
//  }

 private:
  typedef unsigned long int address_t;
  static address_t translate_address(address_t addr)
  {
    // Implementation-specific translation of address
    return addr;
  }
};

void setup_thread(int tid, char *stack, thread_entry_point entry_point)
{
  // initializes env[tid] to use the right stack, and to run from the function 'entry_point', when we'll use
  // siglongjmp to jump into the thread.
  address_t sp = (address_t) stack + STACK_SIZE - sizeof (address_t);
  address_t pc = (address_t) entry_point;
  sigsetjmp (env[tid], 1);
  (env[tid]->__jmpbuf)[JB_SP] = translate_address (sp);
  (env[tid]->__jmpbuf)[JB_PC] = translate_address (pc);
  sigemptyset (&env[tid]->__saved_mask);
}

// Global vars
int current_thread = 0; //current thread index in the ready list
int creations = 0; //0-MAX, counter of the creations of threads
Thread *ready[MAX_THREAD_NUM]; //ready list

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
int uthread_init(int quantum_usecs)
{
  if(quantum_usecs <= 0)
  {
    std::cerr << INVALID_QUANTUM << std::endl;
    return FAIL;
  }
  //TODO: FINISHHHH


//  setup_thread(0, stack0, thread0);
}

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
int uthread_spawn(thread_entry_point entry_point)
{
  if(entry_point == nullptr)
  {
    std::cerr << INVALID_EP << std::endl;
    return FAIL;
  }
  int tid = helper_spawn();
  if(tid < 0)
  {
    std::cerr << ABOVE_MAX << std::endl;
    return FAIL;
  }


  return tid;
}

int helper_spawn(){
  for(int i = 0; i < MAX_THREAD_NUM; ++i)
  {
    if (ready[i] == nullptr){
      return i;
    }
  }
  // no empty spot at the ready list
  return FAIL;
}

void thread_creator(unsigned int tid, thread_entry_point entry_point){
  Thread t = new Thread(tid, entry_point);
  ready[tid] = t;
  current_thread++;

  if(tid==0){ //main thread
    ready[0]->state = RUNNING;
  }

  //TODO: FINISHHHH

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
int uthread_terminate(int tid)
{
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
int uthread_block(int tid)
{
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
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
int uthread_sleep(int num_quantums)
{
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid()
{
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums()
{
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
int uthread_get_quantums(int tid)
{
}



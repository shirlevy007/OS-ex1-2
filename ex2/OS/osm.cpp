#include "osm.h"
#include <sys/time.h>
#include <iostream>
#include <ostream>

#define FACTOR = 5;

void empty_func ()
{}

/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time (unsigned int iterations)
{
  /* 0 is the only invalid number*/
  if (iterations == 0)
  {
    return -1;
  }

//  struct timezone tz;
  struct timeval current_time;
  gettimeofday (&current_time, nullptr);
  time_t pre_time = current_time.tv_sec * 1000000 + current_time.tv_usec;

  int x=0;
  for (int i = 0; i < iterations; i += 5)
  {
    x+=1;
    x+=2;
    x+=3;
    x+=4;
    x+=5;
  }
  gettimeofday (&current_time, nullptr);
  time_t post_time = current_time.tv_sec * 1000000 + current_time.tv_usec;
  time_t delta = post_time-pre_time;

  return (double) ((delta*1000)/iterations);

}

/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time (unsigned int iterations)
{
  /* 0 is the only invalid number*/
  if (iterations == 0)
  {
    return -1;
  }

  struct timeval current_time;
  gettimeofday (&current_time, nullptr);
  time_t pre_time = current_time.tv_sec * 1000000 + current_time.tv_usec;

  for (int i = 0; i < iterations; i += 5)
  {
    empty_func();
    empty_func();
    empty_func();
    empty_func();
    empty_func();

  }
  gettimeofday (&current_time, nullptr);
  time_t post_time = current_time.tv_sec * 1000000 + current_time.tv_usec;
  time_t delta = post_time-pre_time;

  return (double) ((delta*1000)/iterations);

}

/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time (unsigned int iterations){
  /* 0 is the only invalid number*/
  if (iterations == 0)
  {
    return -1;
  }

  struct timeval current_time;
  gettimeofday (&current_time, nullptr);
  time_t pre_time = current_time.tv_sec * 1000000 + current_time.tv_usec;

  for (int i = 0; i < iterations; i += 5)
  {
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;

  }
  gettimeofday (&current_time, nullptr);
  time_t post_time = current_time.tv_sec * 1000000 + current_time.tv_usec;
  time_t delta = post_time-pre_time;

  return (double) ((delta*1000)/iterations);

}

//int main ()
//{
//  std::cout << osm_operation_time (900000) << std::endl;
//  std::cout << osm_function_time (900000) << std::endl;
//  std::cout << osm_syscall_time (10000) << std::endl;
//  return 0;
//}

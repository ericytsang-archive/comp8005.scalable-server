/**
 * header file for the Semaphore class. implementation is in Semaphore.cpp
 *
 * @sourceFile Semaphore.h
 *
 * @program    Threads-Main.out, Processes-Main.out
 *
 * @date       2016-01-15
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note
 *
 * used to wrap a semaphore object so it can be properly deallocated when the
 *   Semaphore instance is destroyed.
 */
#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <semaphore.h>

class Semaphore
{
public:

    Semaphore(bool betweenProcesses,int permits);
    ~Semaphore();
    void post();
    void wait();
    sem_t sem;
};

#endif

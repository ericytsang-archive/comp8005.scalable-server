/**
 * implementation of the Semaphore class declared in Semaphore.h
 *
 * @sourceFile Semaphore.cpp
 *
 * @program    Threads-Main.out, Processes-Main.out
 *
 * @class      Semaphore
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
 * used to wrap an integer of the gmp library so that they will be properly
 *   initialized and deallocated when an instance of this class is constructed
 *   or destroyed.
 */
#include "Semaphore.h"

/**
 * instantiates a Semaphore instance.
 *
 * @class      Semaphore
 *
 * @method     Semaphore
 *
 * @date       2016-01-15
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  Semaphore::Semaphore(bool betweenProcesses,int permits)
 *
 * @param      betweenProcesses true if the semaphore is to be shared between
 *   processes; false if the semaphore is only to be shared between threads of
 *   the same process.
 * @param      permits initial number of permits that the semaphore is set to.
 *
 * @return     an instance of a Semaphore.
 */
Semaphore::Semaphore(bool betweenProcesses,int permits)
{
    sem_init(&sem,betweenProcesses?1:0,permits);
}

/**
 * destructor for the Semaphore.
 *
 * @class      Semaphore
 *
 * @method     ~Semaphore
 *
 * @date       2016-01-15
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  Semaphore::~Semaphore()
 */
Semaphore::~Semaphore()
{
    sem_destroy(&sem);
}

/**
 * posts to the semaphore; returns a permit to the semaphore so it may be
 * acquired by another thread of execution.
 *
 * @class      Semaphore
 *
 * @method     post
 *
 * @date       2016-01-15
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  void Semaphore::post
 */
void Semaphore::post()
{
    sem_post(&sem);
}

/**
 * waits on the semaphore; acquires a permit from the semaphore, or blocks until
 *   one becomes available.
 *
 * @class      Semaphore
 *
 * @method     wait
 *
 * @date       2016-01-15
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  void Semaphore::wait()
 */
void Semaphore::wait()
{
    sem_wait(&sem);
}

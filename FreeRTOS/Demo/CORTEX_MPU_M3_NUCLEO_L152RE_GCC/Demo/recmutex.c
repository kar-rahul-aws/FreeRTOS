/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 *  The tasks defined on this page demonstrate the use of recursive mutexes.
 *
 *  For recursive mutex functionality the created mutex should be created using
 *  xSemaphoreCreateRecursiveMutex(), then be manipulated
 *  using the xSemaphoreTakeRecursive() and xSemaphoreGiveRecursive() API
 *  functions.
 *
 *  This demo creates three tasks all of which access the same recursive mutex:
 *
 *  prvRecursiveMutexControllingTask() has the highest priority so executes
 *  first and grabs the mutex.  It then performs some recursive accesses -
 *  between each of which it sleeps for a short period to let the lower
 *  priority tasks execute.  When it has completed its demo functionality
 *  it gives the mutex back before suspending itself.
 *
 *  prvRecursiveMutexBlockingTask() attempts to access the mutex by performing
 *  a blocking 'take'.  The blocking task has a lower priority than the
 *  controlling task so by the time it executes the mutex has already been
 *  taken by the controlling task,  causing the blocking task to block.  It
 *  does not unblock until the controlling task has given the mutex back,
 *  and it does not actually run until the controlling task has suspended
 *  itself (due to the relative priorities).  When it eventually does obtain
 *  the mutex all it does is give the mutex back prior to also suspending
 *  itself.  At this point both the controlling task and the blocking task are
 *  suspended.
 *
 *  prvRecursiveMutexPollingTask() runs at the idle priority.  It spins round
 *  a tight loop attempting to obtain the mutex with a non-blocking call.  As
 *  the lowest priority task it will not successfully obtain the mutex until
 *  both the controlling and blocking tasks are suspended.  Once it eventually
 *  does obtain the mutex it first unsuspends both the controlling task and
 *  blocking task prior to giving the mutex back - resulting in the polling
 *  task temporarily inheriting the controlling tasks priority.
 */

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Demo app include files. */
#include "recmutex.h"

/* Priorities assigned to the three tasks.  recmuCONTROLLING_TASK_PRIORITY can
 * be overridden by a definition in FreeRTOSConfig.h. */
#ifndef recmuCONTROLLING_TASK_PRIORITY
    #define recmuCONTROLLING_TASK_PRIORITY               ( tskIDLE_PRIORITY + 2 )
#endif
#define recmuBLOCKING_TASK_PRIORITY                      ( tskIDLE_PRIORITY + 1 )
#define recmuPOLLING_TASK_PRIORITY                       ( tskIDLE_PRIORITY + 0 )

/* The recursive call depth. */
#define recmuMAX_COUNT                                   ( 10 )

/* Misc. */
#define recmuSHORT_DELAY                                 ( pdMS_TO_TICKS( 20 ) )
#define recmuNO_DELAY                                    ( ( TickType_t ) 0 )
#define recmu15ms_DELAY                                  ( pdMS_TO_TICKS( 15 ) )

#ifndef recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE
    #define recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE    configMINIMAL_STACK_SIZE
#endif

#define recmuSHARED_MEM_SIZE_WORDS                       ( 8 )
#define recmuSHARED_MEM_SIZE_BYTES                       ( 32 )

/* The three tasks as described at the top of this file. */
static void prvRecursiveMutexControllingTask( void * pvParameters );
static void prvRecursiveMutexBlockingTask( void * pvParameters );
static void prvRecursiveMutexPollingTask( void * pvParameters );

/* The mutex used by the demo. */
static SemaphoreHandle_t xMutex[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* Variables used to detect and latch errors. */
/*static volatile BaseType_t xErrorOccurred[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE }; */
/*static volatile BaseType_t xControllingIsSuspended[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE }; */
/*static volatile BaseType_t xBlockingIsSuspended[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE }; */
/*static volatile UBaseType_t uxControllingCycles[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { 0 }; */
/*static volatile UBaseType_t uxBlockingCycles[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { 0 }; */
/*static volatile UBaseType_t uxPollingCycles[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { 0 }; */

/* Helper variable to accommodate 3 user defined regions */
static volatile UBaseType_t uxHelper[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

#define ERROR_DETECTED                0
#define CONTROLLING_TASK_SUSPENDED    1
#define BLOCKING_TASK_SUSPENDED       2
#define CONTROLLING_CYCLES            3
#define BLOCKING_CYCLES               4
#define POLLING_CYCLES                5

/* Handles of the two higher priority tasks, required so they can be resumed
 * (unsuspended). */
#define CONTROLLING_TASK_IDX          0
#define BLOCKING_TASK_IDX             1
static TaskHandle_t xLocalTaskHandles[ recmuSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( recmuSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };
/*-----------------------------------------------------------*/

void vStartRecursiveMutexTasks( void )
{
    /* Just creates the mutex and the three tasks. */

    xMutex[ 0 ] = xSemaphoreCreateRecursiveMutex();

    static StackType_t xRecursiveMutexControllingTaskStack[ recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xRecursiveMutexBlockingStack[ recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xRecursiveMutexPollingTask[ recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );

    TaskParameters_t xRecursiveMutexControllingTaskParameters =
    {
        .pvTaskCode     = prvRecursiveMutexControllingTask,
        .pcName         = "Rec1",
        .usStackDepth   = recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE,
        .pvParameters   = NULL,
        .uxPriority     = recmuCONTROLLING_TASK_PRIORITY,
        .puxStackBuffer = xRecursiveMutexControllingTaskStack,
        .xRegions       =
        {
            { ( void * ) &( uxHelper[ 0 ] ), recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xMutex[ 0 ] ),   recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { 0,                             0,                         0}
        }
    };
    TaskParameters_t xRecursiveMutexBlockingTaskParameters =
    {
        .pvTaskCode     = prvRecursiveMutexBlockingTask,
        .pcName         = "Rec2",
        .usStackDepth   = recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE,
        .pvParameters   = NULL,
        .uxPriority     = recmuBLOCKING_TASK_PRIORITY,
        .puxStackBuffer = xRecursiveMutexBlockingStack,
        .xRegions       =
        {
            { ( void * ) &( uxHelper[ 0 ] ), recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xMutex[ 0 ] ),   recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { 0,                             0,                         0}
        }
    };
    TaskParameters_t xRecursiveMutexPollingTaskParameters =
    {
        .pvTaskCode     = prvRecursiveMutexPollingTask,
        .pcName         = "Rec3",
        .usStackDepth   = recmuRECURSIVE_MUTEX_TEST_TASK_STACK_SIZE,
        .pvParameters   = NULL,
        .uxPriority     = recmuPOLLING_TASK_PRIORITY,
        .puxStackBuffer = xRecursiveMutexPollingTask,
        .xRegions       =
        {
            { ( void * ) &( uxHelper[ 0 ] ),          recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xMutex[ 0 ] ),            recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xLocalTaskHandles[ 0 ] ), recmuSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) }
        }
    };

    if( xMutex[ 0 ] != NULL )
    {
        /* vQueueAddToRegistry() adds the mutex to the registry, if one is
         * in use.  The registry is provided as a means for kernel aware
         * debuggers to locate mutex and has no purpose if a kernel aware debugger
         * is not being used.  The call to vQueueAddToRegistry() will be removed
         * by the pre-processor if configQUEUE_REGISTRY_SIZE is not defined or is
         * defined to be less than 1. */
        vQueueAddToRegistry( ( QueueHandle_t ) xMutex[ 0 ], "Recursive_Mutex" );

        xTaskCreateRestricted( &( xRecursiveMutexControllingTaskParameters ), &( xLocalTaskHandles[ CONTROLLING_TASK_IDX ] ) );
        xTaskCreateRestricted( &( xRecursiveMutexBlockingTaskParameters ), &( xLocalTaskHandles[ BLOCKING_TASK_IDX ] ) );
        xTaskCreateRestricted( &( xRecursiveMutexPollingTaskParameters ), NULL );
    }
}
/*-----------------------------------------------------------*/

static void prvRecursiveMutexControllingTask( void * pvParameters )
{
    UBaseType_t ux;

    /* Just to remove compiler warning. */
    ( void ) pvParameters;

    for( ; ; )
    {
        /* Should not be able to 'give' the mutex, as we have not yet 'taken'
         * it.   The first time through, the mutex will not have been used yet,
         * subsequent times through, at this point the mutex will be held by the
         * polling task. */
        if( xSemaphoreGiveRecursive( xMutex[ 0 ] ) == pdPASS )
        {
            uxHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        for( ux = 0; ux < recmuMAX_COUNT; ux++ )
        {
            /* We should now be able to take the mutex as many times as
             * we like.
             *
             * The first time through the mutex will be immediately available, on
             * subsequent times through the mutex will be held by the polling task
             * at this point and this Take will cause the polling task to inherit
             * the priority of this task.  In this case the block time must be
             * long enough to ensure the polling task will execute again before the
             * block time expires.  If the block time does expire then the error
             * flag will be set here. */
            if( xSemaphoreTakeRecursive( xMutex[ 0 ], recmu15ms_DELAY ) != pdPASS )
            {
                uxHelper[ ERROR_DETECTED ] = pdTRUE;
            }

            /* Ensure the other task attempting to access the mutex (and the
            * other demo tasks) are able to execute to ensure they either block
            * (where a block time is specified) or return an error (where no
            * block time is specified) as the mutex is held by this task. */
            vTaskDelay( recmuSHORT_DELAY );
        }

        /* For each time we took the mutex, give it back. */
        for( ux = 0; ux < recmuMAX_COUNT; ux++ )
        {
            /* Ensure the other task attempting to access the mutex (and the
             * other demo tasks) are able to execute. */
            vTaskDelay( recmuSHORT_DELAY );

            /* We should now be able to give the mutex as many times as we
             * took it.  When the mutex is available again the Blocking task
             * should be unblocked but not run because it has a lower priority
             * than this task.  The polling task should also not run at this point
             * as it too has a lower priority than this task. */
            if( xSemaphoreGiveRecursive( xMutex[ 0 ] ) != pdPASS )
            {
                uxHelper[ ERROR_DETECTED ] = pdTRUE;
            }

            #if ( configUSE_PREEMPTION == 0 )
                taskYIELD();
            #endif
        }

        /* Having given it back the same number of times as it was taken, we
         * should no longer be the mutex owner, so the next give should fail. */
        if( xSemaphoreGiveRecursive( xMutex[ 0 ] ) == pdPASS )
        {
            uxHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        /* Keep count of the number of cycles this task has performed so a
         * stall can be detected. */
        uxHelper[ CONTROLLING_CYCLES ]++;

        /* Suspend ourselves so the blocking task can execute. */
        uxHelper[ CONTROLLING_TASK_SUSPENDED ] = pdTRUE;
        vTaskSuspend( NULL );
        uxHelper[ CONTROLLING_TASK_SUSPENDED ] = pdFALSE;
    }
}
/*-----------------------------------------------------------*/

static void prvRecursiveMutexBlockingTask( void * pvParameters )
{
    /* Just to remove compiler warning. */
    ( void ) pvParameters;

    for( ; ; )
    {
        /* This task will run while the controlling task is blocked, and the
         * controlling task will block only once it has the mutex - therefore
         * this call should block until the controlling task has given up the
         * mutex, and not actually execute    past this call until the controlling
         * task is suspended.  portMAX_DELAY - 1 is used instead of portMAX_DELAY
         * to ensure the task's state is reported as Blocked and not Suspended in
         * a later call to configASSERT() (within the polling task). */
        if( xSemaphoreTakeRecursive( xMutex[ 0 ], ( portMAX_DELAY - 1 ) ) == pdPASS )
        {
            if( uxHelper[ CONTROLLING_TASK_SUSPENDED ] != pdTRUE )
            {
                /* Did not expect to execute until the controlling task was
                 * suspended. */
                uxHelper[ ERROR_DETECTED ] = pdTRUE;
            }
            else
            {
                /* Give the mutex back before suspending ourselves to allow
                 * the polling task to obtain the mutex. */
                if( xSemaphoreGiveRecursive( xMutex[ 0 ] ) != pdPASS )
                {
                    uxHelper[ ERROR_DETECTED ] = pdTRUE;
                }

                uxHelper[ BLOCKING_TASK_SUSPENDED ] = pdTRUE;
                vTaskSuspend( NULL );
                uxHelper[ BLOCKING_TASK_SUSPENDED ] = pdFALSE;
            }
        }
        else
        {
            /* We should not leave the xSemaphoreTakeRecursive() function
             * until the mutex was obtained. */
            uxHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        /* The controlling and blocking tasks should be in lock step. */
        if( uxHelper[ CONTROLLING_CYCLES ] != ( UBaseType_t ) ( uxHelper[ BLOCKING_CYCLES ] + 1 ) )
        {
            uxHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        /* Keep count of the number of cycles this task has performed so a
         * stall can be detected. */
        uxHelper[ BLOCKING_CYCLES ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvRecursiveMutexPollingTask( void * pvParameters )
{
    /* Just to remove compiler warning. */
    ( void ) pvParameters;

    for( ; ; )
    {
        /* Keep attempting to obtain the mutex.  It should only be obtained when
         * the blocking task has suspended itself, which in turn should only
         * happen when the controlling task is also suspended. */
        if( xSemaphoreTakeRecursive( xMutex[ 0 ], recmuNO_DELAY ) == pdPASS )
        {
            #if ( INCLUDE_eTaskGetState == 1 )
                {
                    configASSERT( eTaskGetState( xLocalTaskHandles[ CONTROLLING_TASK_IDX ] ) == eSuspended );
                    configASSERT( eTaskGetState( xLocalTaskHandles[ BLOCKING_TASK_IDX ] ) == eSuspended );
                }
            #endif /* INCLUDE_eTaskGetState */

            /* Is the blocking task suspended? */
            if( ( uxHelper[ BLOCKING_TASK_SUSPENDED ] != pdTRUE ) || ( uxHelper[ CONTROLLING_TASK_SUSPENDED ] != pdTRUE ) )
            {
                uxHelper[ ERROR_DETECTED ] = pdTRUE;
            }
            else
            {
                /* Keep count of the number of cycles this task has performed
                 * so a stall can be detected. */
                uxHelper[ POLLING_CYCLES ]++;

                /* We can resume the other tasks here even though they have a
                 * higher priority than the polling task.  When they execute they
                 * will attempt to obtain the mutex but fail because the polling
                 * task is still the mutex holder.  The polling task (this task)
                 * will then inherit the higher priority.  The Blocking task will
                 * block indefinitely when it attempts to obtain the mutex, the
                 * Controlling task will only block for a fixed period and an
                 * error will be latched if the polling task has not returned the
                 * mutex by the time this fixed period has expired. */
                vTaskResume( xLocalTaskHandles[ BLOCKING_TASK_IDX ] );
                #if ( configUSE_PREEMPTION == 0 )
                    taskYIELD();
                #endif

                vTaskResume( xLocalTaskHandles[ CONTROLLING_TASK_IDX ] );
                #if ( configUSE_PREEMPTION == 0 )
                    taskYIELD();
                #endif

                /* The other two tasks should now have executed and no longer
                 * be suspended. */
                if( ( uxHelper[ BLOCKING_TASK_SUSPENDED ] == pdTRUE ) || ( uxHelper[ CONTROLLING_TASK_SUSPENDED ] == pdTRUE ) )
                {
                    uxHelper[ ERROR_DETECTED ] = pdTRUE;
                }

                #if ( INCLUDE_uxTaskPriorityGet == 1 )
                    {
                        /* Check priority inherited. */
                        configASSERT( uxTaskPriorityGet( NULL ) == recmuCONTROLLING_TASK_PRIORITY );
                    }
                #endif /* INCLUDE_uxTaskPriorityGet */

                #if ( INCLUDE_eTaskGetState == 1 )
                    {
                        configASSERT( eTaskGetState( xLocalTaskHandles[ CONTROLLING_TASK_IDX ] ) == eBlocked );
                        configASSERT( eTaskGetState( xLocalTaskHandles[ BLOCKING_TASK_IDX ] ) == eBlocked );
                    }
                #endif /* INCLUDE_eTaskGetState */

                /* Release the mutex, dis-inheriting the higher priority again. */
                if( xSemaphoreGiveRecursive( xMutex[ 0 ] ) != pdPASS )
                {
                    uxHelper[ ERROR_DETECTED ] = pdTRUE;
                }

                #if ( INCLUDE_uxTaskPriorityGet == 1 )
                    {
                        /* Check priority dis-inherited. */
                        configASSERT( uxTaskPriorityGet( NULL ) == recmuPOLLING_TASK_PRIORITY );
                    }
                #endif /* INCLUDE_uxTaskPriorityGet */
            }
        }

        #if configUSE_PREEMPTION == 0
            {
                taskYIELD();
            }
        #endif
    }
}
/*-----------------------------------------------------------*/

/* This is called to check that all the created tasks are still running. */
BaseType_t xAreRecursiveMutexTasksStillRunning( void )
{
    BaseType_t xReturn;
    static UBaseType_t uxLastControllingCycles = 0, uxLastBlockingCycles = 0, uxLastPollingCycles = 0;

    /* Is the controlling task still cycling? */
    if( uxLastControllingCycles == uxHelper[ CONTROLLING_CYCLES ] )
    {
        uxHelper[ ERROR_DETECTED ] = pdTRUE;
    }
    else
    {
        uxLastControllingCycles = uxHelper[ CONTROLLING_CYCLES ];
    }

    /* Is the blocking task still cycling? */
    if( uxLastBlockingCycles == uxHelper[ BLOCKING_CYCLES ] )
    {
        uxHelper[ ERROR_DETECTED ] = pdTRUE;
    }
    else
    {
        uxLastBlockingCycles = uxHelper[ BLOCKING_CYCLES ];
    }

    /* Is the polling task still cycling? */
    if( uxLastPollingCycles == uxHelper[ POLLING_CYCLES ] )
    {
        uxHelper[ ERROR_DETECTED ] = pdTRUE;
    }
    else
    {
        uxLastPollingCycles = uxHelper[ POLLING_CYCLES ];
    }

    if( uxHelper[ ERROR_DETECTED ] == pdTRUE )
    {
        xReturn = pdFAIL;
    }
    else
    {
        xReturn = pdPASS;
    }

    return xReturn;
}
/*-----------------------------------------------------------*/

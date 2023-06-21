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
 * Demonstrates and tests mutexes being used from an interrupt.
 */


#include <stdlib.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Demo program include files. */
#include "IntSemTest.h"

#define intsSHARED_MEM_SIZE_WORDS    ( 8 )
#define intsSHARED_MEM_SIZE_BYTES    ( 32 )

/*-----------------------------------------------------------*/

/* The priorities of the test tasks. */
#define intsemMASTER_PRIORITY                   ( tskIDLE_PRIORITY )
#define intsemSLAVE_PRIORITY                    ( tskIDLE_PRIORITY + 1 )

/* The rate at which the tick hook will give the mutex. */
#define intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS    ( 100 )

/* A block time of 0 means 'don't block'. */
#define intsemNO_BLOCK                          0

/* The maximum count value for the counting semaphore given from an
 * interrupt. */
#define intsemMAX_COUNT                         3

/*-----------------------------------------------------------*/

/*
 * The master is a task that receives a mutex that is given from an interrupt -
 * although generally mutexes should not be used given in interrupts (and
 * definitely never taken in an interrupt) there are some circumstances when it
 * may be desirable.
 *
 * The slave task is just used by the master task to force priority inheritance
 * on a mutex that is shared between the master and the slave - which is a
 * separate mutex to that given by the interrupt.
 */
static void vInterruptMutexSlaveTask( void * pvParameters );
static void vInterruptMutexMasterTask( void * pvParameters );

/*
 * A test whereby the master takes the shared and interrupt mutexes in that
 * order, then gives them back in the same order, ensuring the priority
 * inheritance is behaving as expected at each step.
 */
static void prvTakeAndGiveInTheSameOrder( void );

/*
 * A test whereby the master takes the shared and interrupt mutexes in that
 * order, then gives them back in the opposite order to which they were taken,
 * ensuring the priority inheritance is behaving as expected at each step.
 */
static void prvTakeAndGiveInTheOppositeOrder( void );

/*
 * A simple task that interacts with an interrupt using a counting semaphore,
 * primarily for code coverage purposes.
 */
static void vInterruptCountingSemaphoreTask( void * pvParameters );

/*-----------------------------------------------------------*/

/* Flag that will be latched to pdTRUE should any unexpected behaviour be
 * detected in any of the tasks. */
/*static volatile BaseType_t xErrorDetected[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE } ; */

/* Counters that are incremented on each cycle of a test.  This is used to
 * detect a stalled task - a test that is no longer running. */
/*static volatile uint32_t ulMasterLoops[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { 0 } ; */
/*static volatile uint32_t ulCountingSemaphoreLoops[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { 0 } ; */

/* Handles of the test tasks that must be accessed from other test tasks. */
static TaskHandle_t xSlaveHandle[ intsSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

/* A mutex which is given from an interrupt - although generally mutexes should
 * not be used given in interrupts (and definitely never taken in an interrupt)
 * there are some circumstances when it may be desirable. */
/*static SemaphoreHandle_t xISRMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ; */

/* A counting semaphore which is given from an interrupt. */
/*static SemaphoreHandle_t xISRCountingSemaphore[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ; */

/* A mutex which is shared between the master and slave tasks - the master
 * does both sharing of this mutex with the slave and receiving a mutex from the
 * interrupt. */
/*static SemaphoreHandle_t xMasterSlaveMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ; */

/* Flag that allows the master task to control when the interrupt gives or does
 * not give the mutex.  There is no mutual exclusion on this variable, but this is
 * only test code and it should be fine in the 32=bit test environment. */
/*static BaseType_t xOkToGiveMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE } ; */
/*static BaseType_t xOkToGiveCountingSemaphore[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE } ; */

/* Used to coordinate timing between tasks and the interrupt. */
const TickType_t xInterruptGivePeriod = pdMS_TO_TICKS( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS );

static volatile uint32_t xHelper[ intsSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
#define ERROR_DETECTED              0
#define MASTER_LOOPS                1
#define COUNTING_SEMAPHORE_LOOPS    2
#define OK_GIVE_MUTEX               3
#define OK_GIVE_SEMAPHORE           4

static SemaphoreHandle_t xMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };
#define ISR_MUTEX                   0
#define ISR_COUNTING_SEMAPHORE      1
#define MASTER_SLAVE_MUTEX          2

/*-----------------------------------------------------------*/

void vStartInterruptSemaphoreTasks( void )
{
    /* Create the semaphores that are given from an interrupt. */
    xMutex[ ISR_MUTEX ] = xSemaphoreCreateMutex();
    configASSERT( xMutex[ ISR_MUTEX ] );
    xMutex[ ISR_COUNTING_SEMAPHORE ] = xSemaphoreCreateCounting( intsemMAX_COUNT, 0 );
    configASSERT( xMutex[ ISR_COUNTING_SEMAPHORE ] );

    /* Create the mutex that is shared between the master and slave tasks (the
     * master receives a mutex from an interrupt and shares a mutex with the
     * slave. */
    xMutex[ MASTER_SLAVE_MUTEX ] = xSemaphoreCreateMutex();
    configASSERT( xMutex[ MASTER_SLAVE_MUTEX ] );

    static StackType_t xInterruptMutexSlaveTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xInterruptMutexMasterTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xInterruptCountingSemaphoreTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );

    TaskParameters_t xInterruptMutexSlaveTaskParameters =
    {
        .pvTaskCode     = vInterruptMutexSlaveTask,
        .pcName         = "IntMuS",
        .usStackDepth   = configMINIMAL_STACK_SIZE,
        .pvParameters   = NULL,
        .uxPriority     = intsemSLAVE_PRIORITY,
        .puxStackBuffer = xInterruptMutexSlaveTaskStack,
        .xRegions       =
        {
            { ( void * ) &( xMutex[ 0 ] ),  intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xHelper[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { 0,                            0,                        0}
        }
    };
    TaskParameters_t xInterruptMutexMasterTaskParameters =
    {
        .pvTaskCode     = vInterruptMutexMasterTask,
        .pcName         = "IntMuM",
        .usStackDepth   = configMINIMAL_STACK_SIZE,
        .pvParameters   = NULL,
        /* Needs to be privileged because it calls privileged only APIs. */
        .uxPriority     = ( intsemMASTER_PRIORITY | portPRIVILEGE_BIT ),
        .puxStackBuffer = xInterruptMutexMasterTaskStack,
        .xRegions       =
        {
            { ( void * ) &( xSlaveHandle[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xHelper[ 0 ] ),      intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xMutex[ 0 ] ),       intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) }
        }
    };
    TaskParameters_t xInterruptCountingSemaphoreTaskParameters =
    {
        .pvTaskCode     = vInterruptCountingSemaphoreTask,
        .pcName         = "IntCnt",
        .usStackDepth   = configMINIMAL_STACK_SIZE,
        .pvParameters   = NULL,
        /* Needs to be privileged because it calls privileged only APIs --> Set Priority */
        .uxPriority     = ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
        .puxStackBuffer = xInterruptCountingSemaphoreTaskStack,
        .xRegions       =
        {
            { ( void * ) &( xHelper[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xMutex[ 0 ] ),  intsSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { 0,                            0,                        0}
        }
    };

    xTaskCreateRestricted( &( xInterruptMutexSlaveTaskParameters ), &( xSlaveHandle[ 0 ] ) );
    xTaskCreateRestricted( &( xInterruptMutexMasterTaskParameters ), NULL );
    xTaskCreateRestricted( &( xInterruptCountingSemaphoreTaskParameters ), NULL );
}
/*-----------------------------------------------------------*/

static void vInterruptMutexMasterTask( void * pvParameters )
{
    /* Just to avoid compiler warnings. */
    ( void ) pvParameters;

    for( ; ; )
    {
        prvTakeAndGiveInTheSameOrder();

        /* Ensure not to starve out other tests. */
        xHelper[ MASTER_LOOPS ]++;
        vTaskDelay( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS );

        prvTakeAndGiveInTheOppositeOrder();

        /* Ensure not to starve out other tests. */
        xHelper[ MASTER_LOOPS ]++;
        vTaskDelay( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS );
    }
}
/*-----------------------------------------------------------*/

static void prvTakeAndGiveInTheSameOrder( void )
{
    /* Ensure the slave is suspended, and that this task is running at the
     * lower priority as expected as the start conditions. */
    #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xSlaveHandle[ 0 ] ) == eSuspended );
        }
    #endif /* INCLUDE_eTaskGetState */

    if( uxTaskPriorityGet( NULL ) != intsemMASTER_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Take the semaphore that is shared with the slave. */
    if( xSemaphoreTake( xMutex[ MASTER_SLAVE_MUTEX ], intsemNO_BLOCK ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* This task now has the mutex.  Unsuspend the slave so it too
     * attempts to take the mutex. */
    vTaskResume( xSlaveHandle[ 0 ] );

    /* The slave has the higher priority so should now have executed and
     * blocked on the semaphore. */
    #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xSlaveHandle[ 0 ] ) == eBlocked );
        }
    #endif /* INCLUDE_eTaskGetState */

    /* This task should now have inherited the priority of the slave
     * task. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Now wait a little longer than the time between ISR gives to also
     * obtain the ISR mutex. */
    xHelper[ OK_GIVE_MUTEX ] = pdTRUE;

    if( xSemaphoreTake( xMutex[ ISR_MUTEX ], ( xInterruptGivePeriod * 2 ) ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    xMutex[ OK_GIVE_MUTEX ] = pdFALSE;

    /* Attempting to take again immediately should fail as the mutex is
     * already held. */
    if( xSemaphoreTake( xMutex[ ISR_MUTEX ], intsemNO_BLOCK ) != pdFAIL )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Should still be at the priority of the slave task. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Give back the ISR semaphore to ensure the priority is not
     * disinherited as the shared mutex (which the higher priority task is
     * attempting to obtain) is still held. */
    if( xSemaphoreGive( xMutex[ ISR_MUTEX ] ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Finally give back the shared mutex.  This time the higher priority
     * task should run before this task runs again - so this task should have
     * disinherited the priority and the higher priority task should be in the
     * suspended state again. */
    if( xSemaphoreGive( xMutex[ MASTER_SLAVE_MUTEX ] ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    if( uxTaskPriorityGet( NULL ) != intsemMASTER_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xSlaveHandle[ 0 ] ) == eSuspended );
        }
    #endif /* INCLUDE_eTaskGetState */

    /* Reset the mutex ready for the next round. */
    xQueueReset( xMutex[ ISR_MUTEX ] );
}
/*-----------------------------------------------------------*/

static void prvTakeAndGiveInTheOppositeOrder( void )
{
    /* Ensure the slave is suspended, and that this task is running at the
     * lower priority as expected as the start conditions. */
    #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xSlaveHandle[ 0 ] ) == eSuspended );
        }
    #endif /* INCLUDE_eTaskGetState */

    if( uxTaskPriorityGet( NULL ) != intsemMASTER_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Take the semaphore that is shared with the slave. */
    if( xSemaphoreTake( xMutex[ MASTER_SLAVE_MUTEX ], intsemNO_BLOCK ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* This task now has the mutex.  Unsuspend the slave so it too
     * attempts to take the mutex. */
    vTaskResume( xSlaveHandle[ 0 ] );

    /* The slave has the higher priority so should now have executed and
     * blocked on the semaphore. */
    #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xSlaveHandle[ 0 ] ) == eBlocked );
        }
    #endif /* INCLUDE_eTaskGetState */

    /* This task should now have inherited the priority of the slave
     * task. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Now wait a little longer than the time between ISR gives to also
     * obtain the ISR mutex. */
    xHelper[ OK_GIVE_MUTEX ] = pdTRUE;

    if( xSemaphoreTake( xMutex[ ISR_MUTEX ], ( xInterruptGivePeriod * 2 ) ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    xHelper[ OK_GIVE_MUTEX ] = pdFALSE;

    /* Attempting to take again immediately should fail as the mutex is
     * already held. */
    if( xSemaphoreTake( xMutex[ ISR_MUTEX ], intsemNO_BLOCK ) != pdFAIL )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Should still be at the priority of the slave task. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Give back the shared semaphore to ensure the priority is not disinherited
     * as the ISR mutex is still held.  The higher priority slave task should run
     * before this task runs again. */
    if( xSemaphoreGive( xMutex[ MASTER_SLAVE_MUTEX ] ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Should still be at the priority of the slave task as this task still
     * holds one semaphore (this is a simplification in the priority inheritance
     * mechanism. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Give back the ISR semaphore, which should result in the priority being
     * disinherited as it was the last mutex held. */
    if( xSemaphoreGive( xMutex[ ISR_MUTEX ] ) != pdPASS )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    if( uxTaskPriorityGet( NULL ) != intsemMASTER_PRIORITY )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    /* Reset the mutex ready for the next round. */
    xQueueReset( xMutex[ ISR_MUTEX ] );
}
/*-----------------------------------------------------------*/

static void vInterruptMutexSlaveTask( void * pvParameters )
{
    /* Just to avoid compiler warnings. */
    ( void ) pvParameters;

    for( ; ; )
    {
        /* This task starts by suspending itself so when it executes can be
         * controlled by the master task. */
        vTaskSuspend( NULL );

        /* This task will execute when the master task already holds the mutex.
         * Attempting to take the mutex will place this task in the Blocked
         * state. */
        if( xSemaphoreTake( xMutex[ MASTER_SLAVE_MUTEX ], portMAX_DELAY ) != pdPASS )
        {
            xHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        if( xSemaphoreGive( xMutex[ MASTER_SLAVE_MUTEX ] ) != pdPASS )
        {
            xHelper[ ERROR_DETECTED ] = pdTRUE;
        }
    }
}
/*-----------------------------------------------------------*/

static void vInterruptCountingSemaphoreTask( void * pvParameters )
{
    BaseType_t xCount;
    const TickType_t xDelay = pdMS_TO_TICKS( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS ) * ( intsemMAX_COUNT + 1 );

    ( void ) pvParameters;

    for( ; ; )
    {
        /* Expect to start with the counting semaphore empty. */
        if( uxQueueMessagesWaiting( ( QueueHandle_t ) xMutex[ ISR_COUNTING_SEMAPHORE ] ) != 0 )
        {
            xHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        /* Wait until it is expected that the interrupt will have filled the
         * counting semaphore. */
        xHelper[ OK_GIVE_SEMAPHORE ] = pdTRUE;
        vTaskDelay( xDelay );
        xHelper[ OK_GIVE_SEMAPHORE ] = pdFALSE;

        /* Now it is expected that the counting semaphore is full. */
        if( uxQueueMessagesWaiting( ( QueueHandle_t ) xMutex[ ISR_COUNTING_SEMAPHORE ] ) != intsemMAX_COUNT )
        {
            xHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        if( uxQueueSpacesAvailable( ( QueueHandle_t ) xMutex[ ISR_COUNTING_SEMAPHORE ] ) != 0 )
        {
            xHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        xHelper[ COUNTING_SEMAPHORE_LOOPS ]++;

        /* Expect to be able to take the counting semaphore intsemMAX_COUNT
         * times.  A block time of 0 is used as the semaphore should already be
         * there. */
        xCount = 0;

        while( xSemaphoreTake( xMutex[ ISR_COUNTING_SEMAPHORE ], 0 ) == pdPASS )
        {
            xCount++;
        }

        if( xCount != intsemMAX_COUNT )
        {
            xHelper[ ERROR_DETECTED ] = pdTRUE;
        }

        /* Now raise the priority of this task so it runs immediately that the
         * semaphore is given from the interrupt. */
        vTaskPrioritySet( NULL, configMAX_PRIORITIES - 1 );

        /* Block to wait for the semaphore to be given from the interrupt. */
        xHelper[ OK_GIVE_SEMAPHORE ] = pdTRUE;
        xSemaphoreTake( xMutex[ ISR_COUNTING_SEMAPHORE ], portMAX_DELAY );
        xSemaphoreTake( xMutex[ ISR_COUNTING_SEMAPHORE ], portMAX_DELAY );
        xHelper[ OK_GIVE_SEMAPHORE ] = pdFALSE;

        /* Reset the priority so as not to disturbe other tests too much. */
        vTaskPrioritySet( NULL, tskIDLE_PRIORITY );

        xHelper[ COUNTING_SEMAPHORE_LOOPS ]++;
    }
}
/*-----------------------------------------------------------*/

void vInterruptSemaphorePeriodicTest( void )
{
    static TickType_t xLastGiveTime = 0;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    TickType_t xTimeNow;

    /* No mutual exclusion on xOkToGiveMutex, but this is only test code (and
    * only executed on a 32-bit architecture) so ignore that in this case. */
    xTimeNow = xTaskGetTickCountFromISR();

    if( ( ( TickType_t ) ( xTimeNow - xLastGiveTime ) ) >= pdMS_TO_TICKS( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS ) )
    {
        configASSERT( xMutex[ ISR_MUTEX ] );

        if( xHelper[ OK_GIVE_MUTEX ] != pdFALSE )
        {
            /* Null is used as the second parameter in this give, and non-NULL
             * in the other gives for code coverage reasons. */
            xSemaphoreGiveFromISR( xMutex[ ISR_MUTEX ], NULL );

            /* Second give attempt should fail. */
            configASSERT( xSemaphoreGiveFromISR( xMutex[ ISR_MUTEX ], &xHigherPriorityTaskWoken ) == pdFAIL );
        }

        if( xHelper[ OK_GIVE_SEMAPHORE ] != pdFALSE )
        {
            xSemaphoreGiveFromISR( xMutex[ ISR_COUNTING_SEMAPHORE ], &xHigherPriorityTaskWoken );
        }

        xLastGiveTime = xTimeNow;
    }

    /* Remove compiler warnings about the value being set but not used. */
    ( void ) xHigherPriorityTaskWoken;
}
/*-----------------------------------------------------------*/

/* This is called to check that all the created tasks are still running. */
BaseType_t xAreInterruptSemaphoreTasksStillRunning( void )
{
    static uint32_t ulLastMasterLoopCounter = 0, ulLastCountingSemaphoreLoops = 0;

    /* If the demo tasks are running then it is expected that the loop counters
     * will have changed since this function was last called. */
    if( ulLastMasterLoopCounter == xHelper[ MASTER_LOOPS ] )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    ulLastMasterLoopCounter = xHelper[ MASTER_LOOPS ];

    if( ulLastCountingSemaphoreLoops == xHelper[ COUNTING_SEMAPHORE_LOOPS ] )
    {
        xHelper[ ERROR_DETECTED ] = pdTRUE;
    }

    ulLastCountingSemaphoreLoops = xHelper[ COUNTING_SEMAPHORE_LOOPS ]++;

    /* Errors detected in the task itself will have latched xErrorDetected
     * to true. */

    return ( BaseType_t ) !xHelper[ ERROR_DETECTED ];
}
/*-----------------------------------------------------------*/

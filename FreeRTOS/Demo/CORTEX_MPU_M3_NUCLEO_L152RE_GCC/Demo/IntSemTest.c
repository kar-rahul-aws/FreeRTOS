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

#define intsSHARED_MEM_SIZE_WORDS            ( 8 )
#define intsSHARED_MEM_SIZE_BYTES            ( 32 )

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
static volatile BaseType_t xErrorDetected[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE } ;

/* Counters that are incremented on each cycle of a test.  This is used to
 * detect a stalled task - a test that is no longer running. */
static volatile uint32_t ulMasterLoops[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { 0 } ;
static volatile uint32_t ulCountingSemaphoreLoops[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { 0 } ;

/* Handles of the test tasks that must be accessed from other test tasks. */
static TaskHandle_t xSlaveHandle[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ;

/* A mutex which is given from an interrupt - although generally mutexes should
 * not be used given in interrupts (and definitely never taken in an interrupt)
 * there are some circumstances when it may be desirable. */
static SemaphoreHandle_t xISRMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ;

/* A counting semaphore which is given from an interrupt. */
static SemaphoreHandle_t xISRCountingSemaphore[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ;

/* A mutex which is shared between the master and slave tasks - the master
 * does both sharing of this mutex with the slave and receiving a mutex from the
 * interrupt. */
static SemaphoreHandle_t xMasterSlaveMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { NULL } ;

/* Flag that allows the master task to control when the interrupt gives or does
 * not give the mutex.  There is no mutual exclusion on this variable, but this is
 * only test code and it should be fine in the 32=bit test environment. */
static BaseType_t xOkToGiveMutex[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE } ;
static BaseType_t xOkToGiveCountingSemaphore[ intsSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intsSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE } ;

/* Used to coordinate timing between tasks and the interrupt. */
const TickType_t xInterruptGivePeriod = pdMS_TO_TICKS( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS );

/*-----------------------------------------------------------*/

void vStartInterruptSemaphoreTasks( void )
{
    /* Create the semaphores that are given from an interrupt. */
    xISRMutex[ 0 ] = xSemaphoreCreateMutex();
    configASSERT( xISRMutex[ 0 ] );
    xISRCountingSemaphore[ 0 ] = xSemaphoreCreateCounting( intsemMAX_COUNT, 0 );
    configASSERT( xISRCountingSemaphore[ 0 ] );

    /* Create the mutex that is shared between the master and slave tasks (the
     * master receives a mutex from an interrupt and shares a mutex with the
     * slave. */
    xMasterSlaveMutex[ 0 ] = xSemaphoreCreateMutex();
    configASSERT( xMasterSlaveMutex[ 0 ] );

    static StackType_t xInterruptMutexSlaveTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xInterruptMutexMasterTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xInterruptCountingSemaphoreTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );

    TaskParameters_t xInterruptMutexSlaveTaskParameters =
    {
        .pvTaskCode      = vInterruptMutexSlaveTask,
        .pcName          = "IntMuS",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = NULL,
        .uxPriority      = intsemSLAVE_PRIORITY,
        .puxStackBuffer  = xInterruptMutexSlaveTaskStack,
        .xRegions        =  {
                                { ( void * ) &( xMasterSlaveMutex[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorDetected[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                            }
    };
    TaskParameters_t xInterruptMutexMasterTaskParameters =
        {
            .pvTaskCode        = vInterruptMutexMasterTask,
            .pcName            = "IntMuM",
            .usStackDepth    	= configMINIMAL_STACK_SIZE,
            .pvParameters    	= NULL,
			/* Needs to be privileged because it calls privileged only APIs. */
            .uxPriority        = ( intsemMASTER_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer    = xInterruptMutexMasterTaskStack,
            .xRegions        =    {
                                    { ( void * ) &( xSlaveHandle[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( ulMasterLoops[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xOkToGiveMutex[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xISRMutex[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xMasterSlaveMutex[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xErrorDetected[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        }
                                }
        };
    TaskParameters_t xInterruptCountingSemaphoreTaskParameters =
        {
            .pvTaskCode        = vInterruptCountingSemaphoreTask,
            .pcName            = "IntCnt",
            .usStackDepth    = configMINIMAL_STACK_SIZE,
            .pvParameters    = NULL,
			/* Needs to be privileged because it calls privileged only APIs --> Set Priority */
            .uxPriority        = ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer    = xInterruptCountingSemaphoreTaskStack,
            .xRegions        =    {
                                    { ( void * ) &( xOkToGiveCountingSemaphore[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xErrorDetected[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xISRCountingSemaphore[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( ulCountingSemaphoreLoops[ 0 ] ), intsSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        }
                                }
        };

    xTaskCreateRestricted( &( xInterruptMutexSlaveTaskParameters ),  &( xSlaveHandle[ 0 ] ) );
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
        ulMasterLoops[ 0 ]++;
        vTaskDelay( intsemINTERRUPT_MUTEX_GIVE_PERIOD_MS );

        prvTakeAndGiveInTheOppositeOrder();

        /* Ensure not to starve out other tests. */
        ulMasterLoops[ 0 ]++;
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
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Take the semaphore that is shared with the slave. */
    if( xSemaphoreTake( xMasterSlaveMutex[ 0 ], intsemNO_BLOCK ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
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
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Now wait a little longer than the time between ISR gives to also
     * obtain the ISR mutex. */
    xOkToGiveMutex[ 0 ] = pdTRUE;

    if( xSemaphoreTake( xISRMutex[ 0 ], ( xInterruptGivePeriod * 2 ) ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    xOkToGiveMutex[ 0 ] = pdFALSE;

    /* Attempting to take again immediately should fail as the mutex is
     * already held. */
    if( xSemaphoreTake( xISRMutex[ 0 ], intsemNO_BLOCK ) != pdFAIL )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Should still be at the priority of the slave task. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Give back the ISR semaphore to ensure the priority is not
     * disinherited as the shared mutex (which the higher priority task is
     * attempting to obtain) is still held. */
    if( xSemaphoreGive( xISRMutex[ 0 ] ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Finally give back the shared mutex.  This time the higher priority
     * task should run before this task runs again - so this task should have
     * disinherited the priority and the higher priority task should be in the
     * suspended state again. */
    if( xSemaphoreGive( xMasterSlaveMutex[ 0 ] ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    if( uxTaskPriorityGet( NULL ) != intsemMASTER_PRIORITY )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    #if ( INCLUDE_eTaskGetState == 1 )
    {
        configASSERT( eTaskGetState( xSlaveHandle[ 0 ] ) == eSuspended );
    }
    #endif /* INCLUDE_eTaskGetState */

    /* Reset the mutex ready for the next round. */
    xQueueReset( xISRMutex[ 0 ] );
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
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Take the semaphore that is shared with the slave. */
    if( xSemaphoreTake( xMasterSlaveMutex[ 0 ], intsemNO_BLOCK ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
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
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Now wait a little longer than the time between ISR gives to also
     * obtain the ISR mutex. */
    xOkToGiveMutex[ 0 ] = pdTRUE;

    if( xSemaphoreTake( xISRMutex[ 0 ], ( xInterruptGivePeriod * 2 ) ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    xOkToGiveMutex[ 0 ] = pdFALSE;

    /* Attempting to take again immediately should fail as the mutex is
     * already held. */
    if( xSemaphoreTake( xISRMutex[ 0 ], intsemNO_BLOCK ) != pdFAIL )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Should still be at the priority of the slave task. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Give back the shared semaphore to ensure the priority is not disinherited
     * as the ISR mutex is still held.  The higher priority slave task should run
     * before this task runs again. */
    if( xSemaphoreGive( xMasterSlaveMutex[ 0 ] ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Should still be at the priority of the slave task as this task still
     * holds one semaphore (this is a simplification in the priority inheritance
     * mechanism. */
    if( uxTaskPriorityGet( NULL ) != intsemSLAVE_PRIORITY )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Give back the ISR semaphore, which should result in the priority being
     * disinherited as it was the last mutex held. */
    if( xSemaphoreGive( xISRMutex[ 0 ] ) != pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    if( uxTaskPriorityGet( NULL ) != intsemMASTER_PRIORITY )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* Reset the mutex ready for the next round. */
    xQueueReset( xISRMutex[ 0 ] );
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
        if( xSemaphoreTake( xMasterSlaveMutex[ 0 ], portMAX_DELAY ) != pdPASS )
        {
            xErrorDetected[ 0 ] = pdTRUE;
        }

        if( xSemaphoreGive( xMasterSlaveMutex[ 0 ] ) != pdPASS )
        {
            xErrorDetected[ 0 ] = pdTRUE;
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
        if( uxQueueMessagesWaiting( ( QueueHandle_t ) xISRCountingSemaphore[ 0 ] ) != 0 )
        {
            xErrorDetected[ 0 ] = pdTRUE;
        }

        /* Wait until it is expected that the interrupt will have filled the
         * counting semaphore. */
        xOkToGiveCountingSemaphore[ 0 ] = pdTRUE;
        vTaskDelay( xDelay );
        xOkToGiveCountingSemaphore[ 0 ] = pdFALSE;

        /* Now it is expected that the counting semaphore is full. */
        if( uxQueueMessagesWaiting( ( QueueHandle_t ) xISRCountingSemaphore[ 0 ] ) != intsemMAX_COUNT )
        {
            xErrorDetected[ 0 ] = pdTRUE;
        }

        if( uxQueueSpacesAvailable( ( QueueHandle_t ) xISRCountingSemaphore[ 0 ] ) != 0 )
        {
            xErrorDetected[ 0 ] = pdTRUE;
        }

        ulCountingSemaphoreLoops[ 0 ]++;

        /* Expect to be able to take the counting semaphore intsemMAX_COUNT
         * times.  A block time of 0 is used as the semaphore should already be
         * there. */
        xCount = 0;

        while( xSemaphoreTake( xISRCountingSemaphore[ 0 ], 0 ) == pdPASS )
        {
            xCount++;
        }

        if( xCount != intsemMAX_COUNT )
        {
            xErrorDetected[ 0 ] = pdTRUE;
        }

        /* Now raise the priority of this task so it runs immediately that the
         * semaphore is given from the interrupt. */
        vTaskPrioritySet( NULL, configMAX_PRIORITIES - 1 );

        /* Block to wait for the semaphore to be given from the interrupt. */
        xOkToGiveCountingSemaphore[ 0 ] = pdTRUE;
        xSemaphoreTake( xISRCountingSemaphore[ 0 ], portMAX_DELAY );
        xSemaphoreTake( xISRCountingSemaphore[ 0 ], portMAX_DELAY );
        xOkToGiveCountingSemaphore[ 0 ] = pdFALSE;

        /* Reset the priority so as not to disturbe other tests too much. */
        vTaskPrioritySet( NULL, tskIDLE_PRIORITY );

        ulCountingSemaphoreLoops[ 0 ]++;
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
        configASSERT( xISRMutex[ 0 ] );

        if( xOkToGiveMutex[ 0 ] != pdFALSE )
        {
            /* Null is used as the second parameter in this give, and non-NULL
             * in the other gives for code coverage reasons. */
            xSemaphoreGiveFromISR( xISRMutex[ 0 ], NULL );

            /* Second give attempt should fail. */
            configASSERT( xSemaphoreGiveFromISR( xISRMutex[ 0 ], &xHigherPriorityTaskWoken ) == pdFAIL );
        }

        if( xOkToGiveCountingSemaphore[ 0 ] != pdFALSE )
        {
            xSemaphoreGiveFromISR( xISRCountingSemaphore[ 0 ], &xHigherPriorityTaskWoken );
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
    if( ulLastMasterLoopCounter == ulMasterLoops[ 0 ] )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    ulLastMasterLoopCounter = ulMasterLoops[ 0 ];

    if( ulLastCountingSemaphoreLoops == ulCountingSemaphoreLoops[ 0 ] )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    ulLastCountingSemaphoreLoops = ulCountingSemaphoreLoops[ 0 ]++;

    /* Errors detected in the task itself will have latched xErrorDetected
     * to true. */

    return ( BaseType_t ) !xErrorDetected[ 0 ];
}
/*-----------------------------------------------------------*/

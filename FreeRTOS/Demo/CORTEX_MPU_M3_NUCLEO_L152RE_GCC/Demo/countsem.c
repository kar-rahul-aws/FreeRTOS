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
 * Simple demonstration of the usage of counting semaphore.
 */

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Demo program include files. */
#include "countsem.h"

#define countSHARED_MEM_SIZE_WORDS             ( 8 )
#define countSHARED_MEM_SIZE_BYTES             ( 32 )

/* The maximum count value that the semaphore used for the demo can hold. */
#define countMAX_COUNT_VALUE       ( 200 )

/* Constants used to indicate whether or not the semaphore should have been
 * created with its maximum count value, or its minimum count value.  These
 * numbers are used to ensure that the pointers passed in as the task parameters
 * are valid. */
#define countSTART_AT_MAX_COUNT    ( 0xaa )
#define countSTART_AT_ZERO         ( 0x55 )

/* Two tasks are created for the test.  One uses a semaphore created with its
 * count value set to the maximum, and one with the count value set to zero. */
#define countNUM_TEST_TASKS        ( 2 )
#define countDONT_BLOCK            ( 0 )

/*-----------------------------------------------------------*/

/* Flag that will be latched to pdTRUE should any unexpected behavior be
 * detected in any of the tasks. */
static volatile BaseType_t xErrorDetected[ countSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( countSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE };

/*-----------------------------------------------------------*/

/*
 * The demo task.  This simply counts the semaphore up to its maximum value,
 * the counts it back down again.  The result of each semaphore 'give' and
 * 'take' is inspected, with an error being flagged if it is found not to be
 * the expected result.
 */
static void prvCountingSemaphoreTask( void * pvParameters );

/*
 * Utility function to increment the semaphore count value up from zero to
 * countMAX_COUNT_VALUE.
 */
static void prvIncrementSemaphoreCount( SemaphoreHandle_t xSemaphore,
                                        volatile UBaseType_t * puxLoopCounter );

/*
 * Utility function to decrement the semaphore count value up from
 * countMAX_COUNT_VALUE to zero.
 */
static void prvDecrementSemaphoreCount( SemaphoreHandle_t xSemaphore,
                                        volatile UBaseType_t * puxLoopCounter );

/*-----------------------------------------------------------*/

/* The structure that is passed into the task as the task parameter. */
typedef struct COUNT_SEM_STRUCT
{
    /* The semaphore to be used for the demo. */
    SemaphoreHandle_t xSemaphore;

    /* Set to countSTART_AT_MAX_COUNT if the semaphore should be created with
     * its count value set to its max count value, or countSTART_AT_ZERO if it
     * should have been created with its count value set to 0. */
    UBaseType_t uxExpectedStartCount;

    /* Incremented on each cycle of the demo task.  Used to detect a stalled
     * task. */
    volatile UBaseType_t uxLoopCounter;

    uint32_t unused[ 5 ];
} xCountSemStruct;

/* Two structures are defined, one is passed to each test task. */
static xCountSemStruct xParameters1 __attribute__( ( aligned( countSHARED_MEM_SIZE_BYTES ) ) );
static xCountSemStruct xParameters2 __attribute__( ( aligned( countSHARED_MEM_SIZE_BYTES ) ) );

/*-----------------------------------------------------------*/

void vStartCountingSemaphoreTasks( void )
{
    /* Create the semaphores that we are going to use for the test/demo.  The
     * first should be created such that it starts at its maximum count value,
     * the second should be created such that it starts with a count value of zero. */
    xParameters1.xSemaphore = xSemaphoreCreateCounting( countMAX_COUNT_VALUE, countMAX_COUNT_VALUE );
    xParameters1.uxExpectedStartCount = countSTART_AT_MAX_COUNT;
    xParameters1.uxLoopCounter = 0;

    xParameters2.xSemaphore = xSemaphoreCreateCounting( countMAX_COUNT_VALUE, 0 );
    xParameters2.uxExpectedStartCount = 0;
    xParameters2.uxLoopCounter = 0;

    static StackType_t xCountingSemaphoreTaskStack1[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xCountingSemaphoreTaskStack2[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );

    static TaskHandle_t xCountingSemaphoreTask1Handle = NULL;
    static TaskHandle_t xCountingSemaphoreTask2Handle = NULL;

    TaskParameters_t xCountingSemaphoreTask1 =
    {
        .pvTaskCode      = prvCountingSemaphoreTask,
        .pcName          = "CNT1",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = ( void * ) &( xParameters1 ),
        .uxPriority      = tskIDLE_PRIORITY,
        .puxStackBuffer  = xCountingSemaphoreTaskStack1,
        .xRegions        =  {
                                { ( void * ) &( xErrorDetected[ 0 ] ), countSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER )
                                },
                                { &( xParameters1 ), countSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER )
                                },
                                { 0,                0,                    0                                                        }
                            }
    };
    TaskParameters_t xCountingSemaphoreTask2 =
    {
        .pvTaskCode      = prvCountingSemaphoreTask,
        .pcName          = "CNT2",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = ( void * ) &( xParameters2 ),
        .uxPriority      = tskIDLE_PRIORITY,
        .puxStackBuffer  = xCountingSemaphoreTaskStack2,
        .xRegions        =  {
                                { ( void * ) &( xErrorDetected[ 0 ] ), countSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER )
                                },
                                { &( xParameters2 ), countSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER )
                                },
                                { 0,                0,                    0                                                        }
                            }
    };

    /* Were the semaphores created? */
    if( ( xParameters1.xSemaphore != NULL ) || ( xParameters2.xSemaphore != NULL ) )
    {
        /* vQueueAddToRegistry() adds the semaphore to the registry, if one is
         * in use.  The registry is provided as a means for kernel aware
         * debuggers to locate semaphores and has no purpose if a kernel aware
         * debugger is not being used.  The call to vQueueAddToRegistry() will be
         * removed by the pre-processor if configQUEUE_REGISTRY_SIZE is not
         * defined or is defined to be less than 1. */
        vQueueAddToRegistry( ( QueueHandle_t ) xParameters1.xSemaphore, "Counting_Sem_1" );
        vQueueAddToRegistry( ( QueueHandle_t ) xParameters2.xSemaphore, "Counting_Sem_2" );

        /* Create the demo tasks, passing in the semaphore to use as the parameter. */
        xTaskCreateRestricted( &( xCountingSemaphoreTask1 ), &( xCountingSemaphoreTask1Handle ) );
        xTaskCreateRestricted( &( xCountingSemaphoreTask2 ), &( xCountingSemaphoreTask2Handle ) );

#if( configENABLE_ACCESS_CONTROL_LIST == 1 )
        vGrantAccessToQueue( xCountingSemaphoreTask1Handle, xParameters1.xSemaphore );
        vGrantAccessToQueue( xCountingSemaphoreTask2Handle, xParameters2.xSemaphore );
#endif
    }
}
/*-----------------------------------------------------------*/

static void prvDecrementSemaphoreCount( SemaphoreHandle_t xSemaphore,
                                        volatile UBaseType_t * puxLoopCounter )
{
    UBaseType_t ux;

    /* If the semaphore count is at its maximum then we should not be able to
     * 'give' the semaphore. */
    if( xSemaphoreGive( xSemaphore ) == pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* We should be able to 'take' the semaphore countMAX_COUNT_VALUE times. */
    for( ux = 0; ux < countMAX_COUNT_VALUE; ux++ )
    {
        configASSERT( uxSemaphoreGetCount( xSemaphore ) == ( countMAX_COUNT_VALUE - ux ) );

        if( xSemaphoreTake( xSemaphore, countDONT_BLOCK ) != pdPASS )
        {
            /* We expected to be able to take the semaphore. */
            xErrorDetected[ 0 ] = pdTRUE;
        }

        ( *puxLoopCounter )++;
    }

    #if configUSE_PREEMPTION == 0
        taskYIELD();
    #endif

    /* If the semaphore count is zero then we should not be able to    'take'
     * the semaphore. */
    configASSERT( uxSemaphoreGetCount( xSemaphore ) == 0 );

    if( xSemaphoreTake( xSemaphore, countDONT_BLOCK ) == pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }
}
/*-----------------------------------------------------------*/

static void prvIncrementSemaphoreCount( SemaphoreHandle_t xSemaphore,
                                        volatile UBaseType_t * puxLoopCounter )
{
    UBaseType_t ux;

    /* If the semaphore count is zero then we should not be able to    'take'
     * the semaphore. */
    if( xSemaphoreTake( xSemaphore, countDONT_BLOCK ) == pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    /* We should be able to 'give' the semaphore countMAX_COUNT_VALUE times. */
    for( ux = 0; ux < countMAX_COUNT_VALUE; ux++ )
    {
        configASSERT( uxSemaphoreGetCount( xSemaphore ) == ux );

        if( xSemaphoreGive( xSemaphore ) != pdPASS )
        {
            /* We expected to be able to take the semaphore. */
            xErrorDetected[ 0 ] = pdTRUE;
        }

        ( *puxLoopCounter )++;
    }

    #if configUSE_PREEMPTION == 0
        taskYIELD();
    #endif

    /* If the semaphore count is at its maximum then we should not be able to
     * 'give' the semaphore. */
    if( xSemaphoreGive( xSemaphore ) == pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }
}
/*-----------------------------------------------------------*/

static void prvCountingSemaphoreTask( void * pvParameters )
{
    xCountSemStruct * pxParameter;

    #ifdef USE_STDIO
        void vPrintDisplayMessage( const char * const * ppcMessageToSend );

        const char * const pcTaskStartMsg = "Counting semaphore demo started.\r\n";

        /* Queue a message for printing to say the task has started. */
        vPrintDisplayMessage( &pcTaskStartMsg );
    #endif

    /* The semaphore to be used was passed as the parameter. */
    pxParameter = ( xCountSemStruct * ) pvParameters;

    /* Did we expect to find the semaphore already at its max count value, or
     * at zero? */
    if( pxParameter->uxExpectedStartCount == countSTART_AT_MAX_COUNT )
    {
        prvDecrementSemaphoreCount( pxParameter->xSemaphore, &( pxParameter->uxLoopCounter ) );
    }

    /* Now we expect the semaphore count to be 0, so this time there is an
     * error if we can take the semaphore. */
    if( xSemaphoreTake( pxParameter->xSemaphore, 0 ) == pdPASS )
    {
        xErrorDetected[ 0 ] = pdTRUE;
    }

    for( ; ; )
    {
        prvIncrementSemaphoreCount( pxParameter->xSemaphore, &( pxParameter->uxLoopCounter ) );
        prvDecrementSemaphoreCount( pxParameter->xSemaphore, &( pxParameter->uxLoopCounter ) );
    }
}
/*-----------------------------------------------------------*/

BaseType_t xAreCountingSemaphoreTasksStillRunning( void )
{
    static UBaseType_t uxLastCount0 = 0, uxLastCount1 = 0;
    BaseType_t xReturn = pdPASS;

    /* Return fail if any 'give' or 'take' did not result in the expected
     * behaviour. */
    if( xErrorDetected[ 0 ] != pdFALSE )
    {
        xReturn = pdFAIL;
    }

    /* Return fail if either task is not still incrementing its loop counter. */
    if( uxLastCount0 == xParameters1.uxLoopCounter )
    {
        xReturn = pdFAIL;
    }
    else
    {
        uxLastCount0 = xParameters1.uxLoopCounter;
    }

    if( uxLastCount1 == xParameters2.uxLoopCounter )
    {
        xReturn = pdFAIL;
    }
    else
    {
        uxLastCount1 = xParameters2.uxLoopCounter;
    }

    return xReturn;
}
/*-----------------------------------------------------------*/

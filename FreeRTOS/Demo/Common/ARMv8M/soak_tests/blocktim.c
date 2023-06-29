/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sub-license, and/or sell copies of
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
 * This file contains some test scenarios that ensure tasks do not exit queue
 * send or receive functions prematurely.  A description of the tests is
 * included within the code.
 */

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Demo includes. */
#include "blocktim.h"

/* Task priorities and stack sizes.  Allow these to be overridden. */
#ifndef bktPRIMARY_PRIORITY
    #define bktPRIMARY_PRIORITY    ( configMAX_PRIORITIES - 3 )
#endif

#ifndef bktSECONDARY_PRIORITY
    #define bktSECONDARY_PRIORITY    ( configMAX_PRIORITIES - 4 )
#endif

#ifndef bktBLOCK_TIME_TASK_STACK_SIZE
    #define bktBLOCK_TIME_TASK_STACK_SIZE    configMINIMAL_STACK_SIZE
#endif

/* Task behaviour. */
#define bktQUEUE_LENGTH          ( 5 )
#define bktSHORT_WAIT            pdMS_TO_TICKS( ( TickType_t ) 20 )
#define bktPRIMARY_BLOCK_TIME    ( 10 )
#define bktALLOWABLE_MARGIN      ( 15 )
#define bktTIME_TO_BLOCK         ( 175 )
#define bktDONT_BLOCK            ( ( TickType_t ) 0 )
#define bktRUN_INDICATOR         ( ( UBaseType_t ) 0x55 )

/* In case the demo does not have software timers enabled, as this file uses
 * the configTIMER_TASK_PRIORITY setting. */
#ifndef configTIMER_TASK_PRIORITY
    #define configTIMER_TASK_PRIORITY           ( configMAX_PRIORITIES - 1 )
#endif

#define blocktimerSHARED_MEM_SIZE_WORDS         ( 8 )
#define blocktimerSHARED_MEM_SIZE_HALF_WORDS    ( 16 )
#define blocktimerSHARED_MEM_SIZE_BYTES         ( 32 )

/*-----------------------------------------------------------*/

/*
 * The two test tasks.  Their behaviour is commented within the functions.
 */
static void vPrimaryBlockTimeTestTask( void * pvParameters );
static void vSecondaryBlockTimeTestTask( void * pvParameters );

/*
 * Very basic tests to verify the block times are as expected.
 */
static void prvBasicDelayTests( void );

/*-----------------------------------------------------------*/

/* The queue on which the tasks block. */
static QueueHandle_t xTestQueue[ blocktimerSHARED_MEM_SIZE_WORDS ]  __attribute__( ( aligned( 32 ) ) ) = { NULL };

/* Handle to the secondary task is required by the primary task for calls
 * to vTaskSuspend/Resume(). */
static TaskHandle_t xSecondary[ blocktimerSHARED_MEM_SIZE_WORDS ]  __attribute__( ( aligned( 32 ) ) ) = { NULL };

/* Shared array to accommodate 3 user defined regions */
static volatile BaseType_t xSharedArray[ blocktimerSHARED_MEM_SIZE_WORDS ]  __attribute__( ( aligned( 32 ) ) ) = { 0 };

#define ERROR_DETECTED_IDX      0
#define PRIMARY_CYCLES_IDX      1
#define SECONDARY_CYCLES_IDX    2
#define RUN_INDICATOR_IDX       3

/*-----------------------------------------------------------*/

void vCreateBlockTimeTasks( void )
{
    /* Create the queue on which the two tasks block. */
    xTestQueue[ 0 ] = xQueueCreate( bktQUEUE_LENGTH, sizeof( BaseType_t ) );

    static StackType_t xPrimaryBlockTimeTestTaskStack[ bktBLOCK_TIME_TASK_STACK_SIZE ] __attribute__( ( aligned( 32 ) ) );
    static StackType_t xSecondaryBlockTimeTestTaskStack[ bktBLOCK_TIME_TASK_STACK_SIZE ] __attribute__( ( aligned( 32 ) ) );

    if( xTestQueue[ 0 ] != NULL )
    {
        /* vQueueAddToRegistry() adds the queue to the queue registry, if one
         * is in use.  The queue registry is provided as a means for kernel aware
         * debuggers to locate queues and has no purpose if a kernel aware
         * debugger is not being used.  The call to vQueueAddToRegistry() will be
         * removed by the pre-processor if configQUEUE_REGISTRY_SIZE is not
         * defined or is defined to be less than 1. */
        vQueueAddToRegistry( xTestQueue[ 0 ], "Block_Time_Queue" );

        /* Create the two test tasks. */
        TaskParameters_t xPrimaryBlockTimerTestTaskParameters =
        {
            .pvTaskCode     = vPrimaryBlockTimeTestTask,
            .pcName         = "BTest1",
            .usStackDepth   = bktBLOCK_TIME_TASK_STACK_SIZE,
            .pvParameters   = ( void * ) xTestQueue[ 0 ],
            /* Needs to be privileged because it calls privileged only APIs --> Task Suspension */
            .uxPriority     = ( bktPRIMARY_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer = xPrimaryBlockTimeTestTaskStack,
            .xRegions       =
            {
                { 0, 0, 0 },
                #if ( configTOTAL_MPU_REGIONS == 16 )
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                    { 0, 0, 0 },
                #endif
                { 0, 0, 0 },
                { 0, 0, 0 }
            }
        };
        TaskParameters_t xSecondaryBlockTimerTestTaskParameters =
        {
            .pvTaskCode     = vSecondaryBlockTimeTestTask,
            .pcName         = "BTest2",
            .usStackDepth   = bktBLOCK_TIME_TASK_STACK_SIZE,
            .pvParameters   = ( void * ) xTestQueue[ 0 ],
            .uxPriority     = bktSECONDARY_PRIORITY,
            .puxStackBuffer = xSecondaryBlockTimeTestTaskStack,
            .xRegions       =
            {
                { ( void * ) &( xSecondary[ 0 ] ),   32,
                      ( tskMPU_REGION_READ_WRITE | tskMPU_REGION_EXECUTE_NEVER ) },
                { ( void * ) &( xSharedArray[ 0 ] ), 32,
                      ( tskMPU_REGION_READ_WRITE | tskMPU_REGION_EXECUTE_NEVER ) },
                #if ( configTOTAL_MPU_REGIONS == 16 )
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                    { 0,                                 0, 0  },
                #endif
                { ( void * ) &( xTestQueue[ 0 ] ),   32,
                      ( tskMPU_REGION_READ_WRITE | tskMPU_REGION_EXECUTE_NEVER ) }
            }
        };
        xTaskCreateRestricted( &( xPrimaryBlockTimerTestTaskParameters ), NULL );
        xTaskCreateRestricted( &( xSecondaryBlockTimerTestTaskParameters ), &xSecondary[ 0 ] );
    }
}
/*-----------------------------------------------------------*/

static void vPrimaryBlockTimeTestTask( void * pvParameters )
{
    BaseType_t xItem, xData;
    TickType_t xTimeWhenBlocking;
    TickType_t xTimeToBlock, xBlockedTime;

    ( void ) pvParameters;

    for( ; ; )
    {
        /*********************************************************************
         * Test 0
         *
         * Basic vTaskDelay() and vTaskDelayUntil() tests. */
        prvBasicDelayTests();

        /*********************************************************************
         * Test 1
         *
         * Simple block time wakeup test on queue receives. */
        for( xItem = 0; xItem < bktQUEUE_LENGTH; xItem++ )
        {
            /* The queue is empty. Attempt to read from the queue using a block
             * time.  When we wake, ensure the delta in time is as expected. */
            xTimeToBlock = ( TickType_t ) ( bktPRIMARY_BLOCK_TIME << xItem );

            xTimeWhenBlocking = xTaskGetTickCount();

            /* We should unblock after xTimeToBlock having not received
             * anything on the queue. */
            if( xQueueReceive( xTestQueue[ 0 ], &xData, xTimeToBlock ) != errQUEUE_EMPTY )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* How long were we blocked for? */
            xBlockedTime = xTaskGetTickCount() - xTimeWhenBlocking;

            if( xBlockedTime < xTimeToBlock )
            {
                /* Should not have blocked for less than we requested. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            if( xBlockedTime > ( xTimeToBlock + bktALLOWABLE_MARGIN ) )
            {
                /* Should not have blocked for longer than we requested,
                 * although we would not necessarily run as soon as we were
                 * unblocked so a margin is allowed. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }
        }

        /*********************************************************************
         *  Test 2
         *
         *  Simple block time wakeup test on queue sends.
         *
         *  First fill the queue.  It should be empty so all sends should pass. */
        for( xItem = 0; xItem < bktQUEUE_LENGTH; xItem++ )
        {
            if( xQueueSend( xTestQueue[ 0 ], &xItem, bktDONT_BLOCK ) != pdPASS )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            #if configUSE_PREEMPTION == 0
                taskYIELD();
            #endif
        }

        for( xItem = 0; xItem < bktQUEUE_LENGTH; xItem++ )
        {
            /* The queue is full. Attempt to write to the queue using a block
             * time.  When we wake, ensure the delta in time is as expected. */
            xTimeToBlock = ( TickType_t ) ( bktPRIMARY_BLOCK_TIME << xItem );

            xTimeWhenBlocking = xTaskGetTickCount();

            /* We should unblock after xTimeToBlock having not received
             * anything on the queue. */
            if( xQueueSend( xTestQueue[ 0 ], &xItem, xTimeToBlock ) != errQUEUE_FULL )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* How long were we blocked for? */
            xBlockedTime = xTaskGetTickCount() - xTimeWhenBlocking;

            if( xBlockedTime < xTimeToBlock )
            {
                /* Should not have blocked for less than we requested. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            if( xBlockedTime > ( xTimeToBlock + bktALLOWABLE_MARGIN ) )
            {
                /* Should not have blocked for longer than we requested,
                 * although we would not necessarily run as soon as we were
                 * unblocked so a margin is allowed. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }
        }

        /*********************************************************************
         * Test 3
         *
         * Wake the other task, it will block attempting to post to the queue.
         * When we read from the queue the other task will wake, but before it
         * can run we will post to the queue again.  When the other task runs it
         * will find the queue still full, even though it was woken.  It should
         * recognise that its block time has not expired and return to block for
         * the remains of its block time.
         *
         * Wake the other task so it blocks attempting to post to the already
         * full queue. */
        xSharedArray[ RUN_INDICATOR_IDX ] = 0;
        vTaskResume( xSecondary[ 0 ] );

        /* We need to wait a little to ensure the other task executes. */
        while( xSharedArray[ RUN_INDICATOR_IDX ] != bktRUN_INDICATOR )
        {
            /* The other task has not yet executed. */
            vTaskDelay( bktSHORT_WAIT );
        }

        /* Make sure the other task is blocked on the queue. */
        vTaskDelay( bktSHORT_WAIT );
        xSharedArray[ RUN_INDICATOR_IDX ] = 0;

        for( xItem = 0; xItem < bktQUEUE_LENGTH; xItem++ )
        {
            /* Now when we make space on the queue the other task should wake
             * but not execute as this task has higher priority. */
            if( xQueueReceive( xTestQueue[ 0 ], &xData, bktDONT_BLOCK ) != pdPASS )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* Now fill the queue again before the other task gets a chance to
             * execute.  If the other task had executed we would find the queue
             * full ourselves, and the other task have set xRunIndicator. */
            if( xQueueSend( xTestQueue[ 0 ], &xItem, bktDONT_BLOCK ) != pdPASS )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            if( xSharedArray[ RUN_INDICATOR_IDX ] == bktRUN_INDICATOR )
            {
                /* The other task should not have executed. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* Raise the priority of the other task so it executes and blocks
             * on the queue again. */
            vTaskPrioritySet( xSecondary[ 0 ], bktPRIMARY_PRIORITY + 2 );

            /* The other task should now have re-blocked without exiting the
             * queue function. */
            if( xSharedArray[ RUN_INDICATOR_IDX ] == bktRUN_INDICATOR )
            {
                /* The other task should not have executed outside of the
                 * queue function. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* Set the priority back down. */
            vTaskPrioritySet( xSecondary[ 0 ], bktSECONDARY_PRIORITY );
        }

        /* Let the other task timeout.  When it unblockes it will check that it
         * unblocked at the correct time, then suspend itself. */
        while( xSharedArray[ RUN_INDICATOR_IDX ] != bktRUN_INDICATOR )
        {
            vTaskDelay( bktSHORT_WAIT );
        }

        vTaskDelay( bktSHORT_WAIT );
        xSharedArray[ RUN_INDICATOR_IDX ] = 0;

        /*********************************************************************
         * Test 4
         *
         * As per test 3 - but with the send and receive the other way around.
         * The other task blocks attempting to read from the queue.
         *
         * Empty the queue.  We should find that it is full. */
        for( xItem = 0; xItem < bktQUEUE_LENGTH; xItem++ )
        {
            if( xQueueReceive( xTestQueue[ 0 ], &xData, bktDONT_BLOCK ) != pdPASS )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }
        }

        /* Wake the other task so it blocks attempting to read from  the
         * already	empty queue. */
        vTaskResume( xSecondary[ 0 ] );

        /* We need to wait a little to ensure the other task executes. */
        while( xSharedArray[ RUN_INDICATOR_IDX ] != bktRUN_INDICATOR )
        {
            vTaskDelay( bktSHORT_WAIT );
        }

        vTaskDelay( bktSHORT_WAIT );
        xSharedArray[ RUN_INDICATOR_IDX ] = 0;

        for( xItem = 0; xItem < bktQUEUE_LENGTH; xItem++ )
        {
            /* Now when we place an item on the queue the other task should
             * wake but not execute as this task has higher priority. */
            if( xQueueSend( xTestQueue[ 0 ], &xItem, bktDONT_BLOCK ) != pdPASS )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* Now empty the queue again before the other task gets a chance to
             * execute.  If the other task had executed we would find the queue
             * empty ourselves, and the other task would be suspended. */
            if( xQueueReceive( xTestQueue[ 0 ], &xData, bktDONT_BLOCK ) != pdPASS )
            {
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            if( xSharedArray[ RUN_INDICATOR_IDX ] == bktRUN_INDICATOR )
            {
                /* The other task should not have executed. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            /* Raise the priority of the other task so it executes and blocks
             * on the queue again. */
            vTaskPrioritySet( xSecondary[ 0 ], bktPRIMARY_PRIORITY + 2 );

            /* The other task should now have re-blocked without exiting the
             * queue function. */
            if( xSharedArray[ RUN_INDICATOR_IDX ] == bktRUN_INDICATOR )
            {
                /* The other task should not have executed outside of the
                 * queue function. */
                xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
            }

            vTaskPrioritySet( xSecondary[ 0 ], bktSECONDARY_PRIORITY );
        }

        /* Let the other task timeout.  When it unblockes it will check that it
         * unblocked at the correct time, then suspend itself. */
        while( xSharedArray[ RUN_INDICATOR_IDX ] != bktRUN_INDICATOR )
        {
            vTaskDelay( bktSHORT_WAIT );
        }

        vTaskDelay( bktSHORT_WAIT );

        xSharedArray[ PRIMARY_CYCLES_IDX ]++;
    }
}
/*-----------------------------------------------------------*/

static void vSecondaryBlockTimeTestTask( void * pvParameters )
{
    TickType_t xTimeWhenBlocking, xBlockedTime;
    BaseType_t xData;

    ( void ) pvParameters;

    for( ; ; )
    {
        /*********************************************************************
         * Test 0, 1 and 2
         *
         * This task does not participate in these tests. */
        vTaskSuspend( NULL );

        /*********************************************************************
         * Test 3
         *
         * The first thing we do is attempt to read from the queue.  It should be
         * full so we block.  Note the time before we block so we can check the
         * wake time is as per that expected. */
        xTimeWhenBlocking = xTaskGetTickCount();

        /* We should unblock after bktTIME_TO_BLOCK having not sent anything to
         * the queue. */
        xData = 0;
        xSharedArray[ RUN_INDICATOR_IDX ] = bktRUN_INDICATOR;

        if( xQueueSend( xTestQueue[ 0 ], &xData, bktTIME_TO_BLOCK ) != errQUEUE_FULL )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        /* How long were we inside the send function? */
        xBlockedTime = xTaskGetTickCount() - xTimeWhenBlocking;

        /* We should not have blocked for less time than bktTIME_TO_BLOCK. */
        if( xBlockedTime < bktTIME_TO_BLOCK )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        /* We should of not blocked for much longer than bktALLOWABLE_MARGIN
         * either.  A margin is permitted as we would not necessarily run as
         * soon as we unblocked. */
        if( xBlockedTime > ( bktTIME_TO_BLOCK + bktALLOWABLE_MARGIN ) )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        /* Suspend ready for test 3. */
        xSharedArray[ RUN_INDICATOR_IDX ] = bktRUN_INDICATOR;
        vTaskSuspend( NULL );

        /*********************************************************************
         * Test 4
         *
         * As per test three, but with the send and receive reversed. */
        xTimeWhenBlocking = xTaskGetTickCount();

        /* We should unblock after bktTIME_TO_BLOCK having not received
         * anything on the queue. */
        xSharedArray[ RUN_INDICATOR_IDX ] = bktRUN_INDICATOR;

        if( xQueueReceive( xTestQueue[ 0 ], &xData, bktTIME_TO_BLOCK ) != errQUEUE_EMPTY )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        xBlockedTime = xTaskGetTickCount() - xTimeWhenBlocking;

        /* We should not have blocked for less time than bktTIME_TO_BLOCK. */
        if( xBlockedTime < bktTIME_TO_BLOCK )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        /* We should of not blocked for much longer than bktALLOWABLE_MARGIN
         * either.  A margin is permitted as we would not necessarily run as soon
         * as we unblocked. */
        if( xBlockedTime > ( bktTIME_TO_BLOCK + bktALLOWABLE_MARGIN ) )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        xSharedArray[ RUN_INDICATOR_IDX ] = bktRUN_INDICATOR;

        xSharedArray[ SECONDARY_CYCLES_IDX ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvBasicDelayTests( void )
{
    TickType_t xPreTime, xPostTime, x, xLastUnblockTime, xExpectedUnblockTime;
    const TickType_t xPeriod = 75, xCycles = 5, xAllowableMargin = ( bktALLOWABLE_MARGIN >> 1 ), xHalfPeriod = xPeriod / ( TickType_t ) 2;
    BaseType_t xDidBlock;

    /* Temporarily increase priority so the timing is more accurate, but not so
     * high as to disrupt the timer tests. */
    vTaskPrioritySet( NULL, configTIMER_TASK_PRIORITY - 1 );

    /* Crude check to too see that vTaskDelay() blocks for the expected
     * period. */
    xPreTime = xTaskGetTickCount();
    vTaskDelay( bktTIME_TO_BLOCK );
    xPostTime = xTaskGetTickCount();

    /* The priority is higher, so the allowable margin is halved when compared
     * to the other tests in this file. */
    if( ( xPostTime - xPreTime ) > ( bktTIME_TO_BLOCK + xAllowableMargin ) )
    {
        xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
    }

    /* Now crude tests to check the vTaskDelayUntil() functionality. */
    xPostTime = xTaskGetTickCount();
    xLastUnblockTime = xPostTime;

    for( x = 0; x < xCycles; x++ )
    {
        /* Calculate the next expected unblock time from the time taken before
         * this loop was entered. */
        xExpectedUnblockTime = xPostTime + ( x * xPeriod );

        vTaskDelayUntil( &xLastUnblockTime, xPeriod );

        if( ( xTaskGetTickCount() - xExpectedUnblockTime ) > ( bktTIME_TO_BLOCK + xAllowableMargin ) )
        {
            xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
        }

        xSharedArray[ PRIMARY_CYCLES_IDX ]++;
    }

    /* Crude tests for return value of xTaskDelayUntil().  First a standard block
     * should return that the task does block. */
    xDidBlock = xTaskDelayUntil( &xLastUnblockTime, xPeriod );

    if( xDidBlock != pdTRUE )
    {
        xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
    }

    /* Now delay a few ticks so repeating the above block period will not block for
     * the full amount of time, but will still block. */
    vTaskDelay( xHalfPeriod );
    xDidBlock = xTaskDelayUntil( &xLastUnblockTime, xPeriod );

    if( xDidBlock != pdTRUE )
    {
        xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
    }

    /* This time block for longer than xPeriod before calling xTaskDelayUntil() so
     * the call to xTaskDelayUntil() should not block. */
    vTaskDelay( xPeriod );
    xDidBlock = xTaskDelayUntil( &xLastUnblockTime, xPeriod );

    if( xDidBlock != pdFALSE )
    {
        xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
    }

    /* Catch up. */
    xDidBlock = xTaskDelayUntil( &xLastUnblockTime, xPeriod );

    if( xDidBlock != pdTRUE )
    {
        xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
    }

    /* Again block for slightly longer than a period so ensure the time is in the
     * past next time xTaskDelayUntil() gets called. */
    vTaskDelay( xPeriod + xAllowableMargin );
    xDidBlock = xTaskDelayUntil( &xLastUnblockTime, xPeriod );

    if( xDidBlock != pdFALSE )
    {
        xSharedArray[ ERROR_DETECTED_IDX ] = pdTRUE;
    }

    /* Reset to the original task priority ready for the other tests. */
    vTaskPrioritySet( NULL, bktPRIMARY_PRIORITY );
}
/*-----------------------------------------------------------*/

BaseType_t xAreBlockTimeTestTasksStillRunning( void )
{
    static BaseType_t xLastPrimaryCycleCount = 0, xLastSecondaryCycleCount = 0;
    BaseType_t xReturn = pdPASS;

    /* Have both tasks performed at least one cycle since this function was
     * last called? */
    if( xSharedArray[ PRIMARY_CYCLES_IDX ] == xLastPrimaryCycleCount )
    {
        xReturn = pdFAIL;
    }

    if( xSharedArray[ SECONDARY_CYCLES_IDX ] == xLastSecondaryCycleCount )
    {
        xReturn = pdFAIL;
    }

    if( xSharedArray[ ERROR_DETECTED_IDX ] == pdTRUE )
    {
        xReturn = pdFAIL;
    }

    xLastSecondaryCycleCount = xSharedArray[ SECONDARY_CYCLES_IDX ];
    xLastPrimaryCycleCount = xSharedArray[ PRIMARY_CYCLES_IDX ];

    return xReturn;
}

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
 * Tests the behaviour of timers.  Some timers are created before the scheduler
 * is started, and some after.
 */

/* Standard includes. */
#include <string.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/* Demo program include files. */
#include "TimerDemo.h"

#if ( configTIMER_TASK_PRIORITY < 1 )
    #error configTIMER_TASK_PRIORITY must be set to at least 1 for this test/demo to function correctly.
#endif

#define tmrdemoDONT_BLOCK                    ( ( TickType_t ) 0 )
#define tmrdemoONE_SHOT_TIMER_PERIOD         ( xHelper[ BASE_PERIOD ] * ( TickType_t ) 3 )
#define tmrdemoNUM_TIMER_RESETS              ( ( uint8_t ) 10 )

#ifndef tmrTIMER_TEST_TASK_STACK_SIZE
    #define tmrTIMER_TEST_TASK_STACK_SIZE    configMINIMAL_STACK_SIZE
#endif

#define tmrSHARED_MEM_SIZE_WORDS             ( 8 )
#define tmrSHARED_MEM_SIZE_BYTES             ( 32 )

/*-----------------------------------------------------------*/

/* The callback functions used by the timers.  These each increment a counter
 * to indicate which timer has expired.  The auto-reload timers that are used by
 * the test task (as opposed to being used from an ISR) all share the same
 * prvAutoReloadTimerCallback() callback function, and use the ID of the
 * pxExpiredTimer parameter passed into that function to know which counter to
 * increment.  The other timers all have their own unique callback function and
 * simply increment their counters without using the callback function parameter. */
static void prvAutoReloadTimerCallback( TimerHandle_t pxExpiredTimer );
static void prvOneShotTimerCallback( TimerHandle_t pxExpiredTimer );
static void prvTimerTestTask( void * pvParameters );
static void prvISRAutoReloadTimerCallback( TimerHandle_t pxExpiredTimer );
static void prvISROneShotTimerCallback( TimerHandle_t pxExpiredTimer );

/* The test functions used by the timer test task.  These manipulate the auto
 * reload and one-shot timers in various ways, then delay, then inspect the timers
 * to ensure they have behaved as expected. */
static void prvTest1_CreateTimersWithoutSchedulerRunning( void );
static void prvTest2_CheckTaskAndTimersInitialState( void );
static void prvTest3_CheckAutoReloadExpireRates( void );
static void prvTest4_CheckAutoReloadTimersCanBeStopped( void );
static void prvTest5_CheckBasicOneShotTimerBehaviour( void );
static void prvTest6_CheckAutoReloadResetBehaviour( void );
static void prvTest7_CheckBacklogBehaviour( void );
static void prvResetStartConditionsForNextIteration( void );

/*-----------------------------------------------------------*/

/* Flag that will be latched to pdFAIL should any unexpected behaviour be
 * detected in any of the demo tests. */
/*static volatile BaseType_t xTestStatus[ tmrSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { pdPASS }; */

/* Flag indicating whether the testing includes the backlog demo.  The backlog
 * demo can be disruptive to other demos because the timer backlog is created by
 * calling xTaskCatchUpTicks(). */
static uint8_t ucIsBacklogDemoEnabled[ tmrSHARED_MEM_SIZE_BYTES ]  __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { ( uint8_t ) pdFALSE };

/* Counter that is incremented on each cycle of a test.  This is used to
 * detect a stalled task - a test that is no longer running. */
/*static volatile uint32_t ulLoopCounter[ tmrSHARED_MEM_SIZE_WORDS ]  __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { 0 }; */

/* A set of auto-reload timers - each of which use the same callback function.
 * The callback function uses the timer ID to index into, and then increment, a
 * counter in the ucAutoReloadTimerCounters[] array.  The callback function stops
 * xAutoReloadTimers[0] during its callback if ucIsStopNeededInTimerZeroCallback is
 * pdTRUE.  The auto-reload timers referenced from xAutoReloadTimers[] are used by
 * the prvTimerTestTask task. */
static TimerHandle_t xAutoReloadTimers[ 16 ] __attribute__( ( aligned( 64 ) ) ) = { 0 };
static uint8_t ucAutoReloadTimerCounters[ tmrSHARED_MEM_SIZE_BYTES ] __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
static uint8_t ucIsStopNeededInTimerZeroCallback = ( uint8_t ) pdFALSE;

/* The one-shot timer is configured to use a callback function that increments
 * ucOneShotTimerCounter[ 0 ] each time it gets called. */
/*static TimerHandle_t xOneShotTimer[ tmrSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { NULL }; */
static uint8_t ucOneShotTimerCounter[ tmrSHARED_MEM_SIZE_BYTES ] __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { ( uint8_t ) 0 };
#define ONE_SHOT_TIMER    ( configTIMER_QUEUE_LENGTH + 2 )

/* The ISR reload timer is controlled from the tick hook to exercise the timer
 * API functions that can be used from an ISR.  It is configured to increment
 * ucISRReloadTimerCounter each time its callback function is executed. */
static TimerHandle_t xISRAutoReloadTimer = NULL;
static uint8_t ucISRAutoReloadTimerCounter = ( uint8_t ) 0;

/* The ISR one-shot timer is controlled from the tick hook to exercise the timer
 * API functions that can be used from an ISR.  It is configured to increment
 * ucISRReloadTimerCounter each time its callback function is executed. */
static TimerHandle_t xISROneShotTimer = NULL;
static uint8_t ucISROneShotTimerCounter = ( uint8_t ) 0;

/* The period of all the timers are a multiple of the base period.  The base
 * period is configured by the parameter to vStartTimerDemoTask(). */
/*static TickType_t xBasePeriod[ tmrSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { 0 }; */

static uint32_t xHelper[ tmrSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( tmrSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
#define TEST_STATUS                  ( 0 )
#define LOOP_COUNTER                 ( 1 )
#define AUTO_RELOAD_TIMER_COUNTER    ( 2 )
#define ONE_SHOT_TIMER_COUNTER       ( 3 )
#define BASE_PERIOD                  ( 4 )

/*-----------------------------------------------------------*/

void vStartTimerDemoTask( TickType_t xBasePeriodIn )
{
    static StackType_t xTimerTestTaskStack[ tmrTIMER_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( tmrTIMER_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
    TaskParameters_t xTimerTestTaskParameters =
    {
        .pvTaskCode     = prvTimerTestTask,
        .pcName         = "Tmr Tst",
        .usStackDepth   = tmrTIMER_TEST_TASK_STACK_SIZE,
        .pvParameters   = NULL,
        /* Needs to be privileged because it calls privileged only APIs --> Set Priority */
        .uxPriority     = ( ( configTIMER_TASK_PRIORITY - 1 ) | portPRIVILEGE_BIT ),
        .puxStackBuffer = xTimerTestTaskStack,
        .xRegions       =
        {
            { ( void * ) &( xHelper[ 0 ] ),                tmrSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( ucIsBacklogDemoEnabled[ 0 ] ), tmrSHARED_MEM_SIZE_BYTES,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) },
            { ( void * ) &( xAutoReloadTimers[ 0 ] ),      64,
              ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER ) }
        }
    };

    /* Start with the timer and counter arrays clear - this is only necessary
     * where the compiler does not clear them automatically on start up. */
    memset( ucAutoReloadTimerCounters, 0x00, sizeof( ucAutoReloadTimerCounters ) );
    memset( xAutoReloadTimers, 0x00, sizeof( xAutoReloadTimers ) );

    /* Store the period from which all the timer periods will be generated from
     * (multiples of). */
    xHelper[ BASE_PERIOD ] = xBasePeriodIn;
    xHelper[ TEST_STATUS ] = pdPASS;

    /* Create a set of timers for use by this demo/test. */
    prvTest1_CreateTimersWithoutSchedulerRunning();


    /* Create a one-shot timer for use later on in this test.  For test purposes it
     * is created as an auto-reload timer then converted to a one-shot timer. */
    xAutoReloadTimers[ ONE_SHOT_TIMER ] = xTimerCreate( "Oneshot Timer",              /* Text name to facilitate debugging.  The kernel does not use this itself. */
                                                        tmrdemoONE_SHOT_TIMER_PERIOD, /* The period for the timer. */
                                                        pdFALSE,                      /* Auto-reload is false, so created as a one-shot timer. */
                                                        ( void * ) 0,                 /* The timer identifier.  Initialise to 0, then increment each time it is called. */
                                                        prvOneShotTimerCallback );    /* The callback to be called when the timer expires. */

    configASSERT( xAutoReloadTimers[ ONE_SHOT_TIMER ] );


    /* Create the task that will control and monitor the timers.  This is
     * created at a lower priority than the timer service task to ensure, as
     * far as it is concerned, commands on timers are acted on immediately
     * (sending a command to the timer service task will unblock the timer service
     * task, which will then preempt this task). */
    xTaskCreateRestricted( &xTimerTestTaskParameters, NULL );
}
/*-----------------------------------------------------------*/

void vTimerDemoIncludeBacklogTests( BaseType_t includeBacklogTests )
{
    ucIsBacklogDemoEnabled[ 0 ] = ( uint8_t ) includeBacklogTests;
}
/*-----------------------------------------------------------*/

static void prvTimerTestTask( void * pvParameters )
{
    ( void ) pvParameters;

    /* Purely for test coverage purposes - change and query the reload mode to
     * auto-reload then back to one-shot. */

    /* Change timer to auto-reload. */
    vTimerSetReloadMode( xAutoReloadTimers[ ONE_SHOT_TIMER ], pdTRUE );

    /* Timer should now be auto-reload. */
    configASSERT( uxTimerGetReloadMode( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) == pdTRUE );

    /* Change timer to one-shot, which is what is needed for this test. */
    vTimerSetReloadMode( xAutoReloadTimers[ ONE_SHOT_TIMER ], pdFALSE );

    /* Check change to one-shot was successful. */
    configASSERT( uxTimerGetReloadMode( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) == pdFALSE );

    /* Ensure all the timers are in their expected initial state.  This
     * depends on the timer service task having a higher priority than this task. */
    prvTest2_CheckTaskAndTimersInitialState();

    for( ; ; )
    {
        /* Check the auto-reload timers expire at the expected/correct rates. */
        prvTest3_CheckAutoReloadExpireRates();

        /* Check the auto-reload timers can be stopped correctly, and correctly
         * report their state. */
        prvTest4_CheckAutoReloadTimersCanBeStopped();

        /* Check the one-shot timer only calls its callback once after it has been
         * started, and that it reports its state correctly. */
        prvTest5_CheckBasicOneShotTimerBehaviour();

        /* Check timer reset behaviour. */
        prvTest6_CheckAutoReloadResetBehaviour();

        /* Check timer behaviour when the timer task gets behind in its work. */
        if( ucIsBacklogDemoEnabled[ 0 ] == ( uint8_t ) pdTRUE )
        {
            prvTest7_CheckBacklogBehaviour();
        }

        /* Start the timers again to restart all the tests over again. */
        prvResetStartConditionsForNextIteration();
    }
}
/*-----------------------------------------------------------*/

/* This is called to check that the created task is still running and has not
 * detected any errors. */
BaseType_t xAreTimerDemoTasksStillRunning( TickType_t xCycleFrequency )
{
    static uint32_t ulLastLoopCounter = 0UL;
    TickType_t xMaxBlockTimeUsedByTheseTests, xLoopCounterIncrementTimeMax;
    static TickType_t xIterationsWithoutCounterIncrement = ( TickType_t ) 0, xLastCycleFrequency;

    if( xLastCycleFrequency != xCycleFrequency )
    {
        /* The cycle frequency has probably become much faster due to an error
         * elsewhere.  Start counting Iterations again. */
        xIterationsWithoutCounterIncrement = ( TickType_t ) 0;
        xLastCycleFrequency = xCycleFrequency;
    }

    /* Calculate the maximum number of times that it is permissible for this
     * function to be called without xHelper[ LOOP_COUNTER ] being incremented.  This is
     * necessary because the tests in this file block for extended periods, and the
     * block period might be longer than the time between calls to this function. */
    xMaxBlockTimeUsedByTheseTests = ( ( TickType_t ) configTIMER_QUEUE_LENGTH ) * xHelper[ BASE_PERIOD ];
    xLoopCounterIncrementTimeMax = ( xMaxBlockTimeUsedByTheseTests / xCycleFrequency ) + 1;

    /* If the demo task is still running then the loop counter is expected to
     * have incremented every xLoopCounterIncrementTimeMax calls. */
    if( ulLastLoopCounter == xHelper[ LOOP_COUNTER ] )
    {
        xIterationsWithoutCounterIncrement++;

        if( xIterationsWithoutCounterIncrement > xLoopCounterIncrementTimeMax )
        {
            /* The tests appear to be no longer running (stalled). */
            xHelper[ TEST_STATUS ] = pdFAIL;
        }
    }
    else
    {
        /* xHelper[ LOOP_COUNTER ] changed, so the count of times this function was called
         * without a change can be reset to zero. */
        xIterationsWithoutCounterIncrement = ( TickType_t ) 0;
    }

    ulLastLoopCounter = xHelper[ LOOP_COUNTER ];

    /* Errors detected in the task itself will have latched xHelper[ TEST_STATUS ]
     * to pdFAIL. */

    return xHelper[ TEST_STATUS ];
}
/*-----------------------------------------------------------*/

static void prvTest1_CreateTimersWithoutSchedulerRunning( void )
{
    TickType_t xTimer;

    for( xTimer = 0; xTimer < configTIMER_QUEUE_LENGTH; xTimer++ )
    {
        /* As the timer queue is not yet full, it should be possible to both
         * create and start a timer.  These timers are being started before the
         * scheduler has been started, so their block times should get set to zero
         * within the timer API itself. */
        xAutoReloadTimers[ xTimer ] = xTimerCreate( "FR Timer",                                                 /* Text name to facilitate debugging.  The kernel does not use this itself. */
                                                    ( ( xTimer + ( TickType_t ) 1 ) * xHelper[ BASE_PERIOD ] ), /* The period for the timer.  The plus 1 ensures a period of zero is not specified. */
                                                    pdTRUE,                                                     /* Auto-reload is set to true. */
                                                    ( void * ) xTimer,                                          /* An identifier for the timer as all the auto-reload timers use the same callback. */
                                                    prvAutoReloadTimerCallback );                               /* The callback to be called when the timer expires. */

        if( xAutoReloadTimers[ xTimer ] == NULL )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
        else
        {
            configASSERT( strcmp( pcTimerGetName( xAutoReloadTimers[ xTimer ] ), "FR Timer" ) == 0 );

            /* The scheduler has not yet started, so the block period of
             * portMAX_DELAY should just get set to zero in xTimerStart().  Also,
             * the timer queue is not yet full so xTimerStart() should return
             * pdPASS. */
            if( xTimerStart( xAutoReloadTimers[ xTimer ], portMAX_DELAY ) != pdPASS )
            {
                xHelper[ TEST_STATUS ] = pdFAIL;
                configASSERT( xHelper[ TEST_STATUS ] );
            }
        }
    }

    /* The timers queue should now be full, so it should be possible to create
     * another timer, but not possible to start it (the timer queue will not get
     * drained until the scheduler has been started. */
    xAutoReloadTimers[ configTIMER_QUEUE_LENGTH ] = xTimerCreate( "FR Timer",                                            /* Text name to facilitate debugging.  The kernel does not use this itself. */
                                                                  ( configTIMER_QUEUE_LENGTH * xHelper[ BASE_PERIOD ] ), /* The period for the timer. */
                                                                  pdTRUE,                                                /* Auto-reload is set to true. */
                                                                  ( void * ) xTimer,                                     /* An identifier for the timer as all the auto-reload timers use the same callback. */
                                                                  prvAutoReloadTimerCallback );                          /* The callback executed when the timer expires. */

    if( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH ] == NULL )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }
    else
    {
        if( xTimerStart( xAutoReloadTimers[ xTimer ], portMAX_DELAY ) == pdPASS )
        {
            /* This time it would not be expected that the timer could be
             * started at this point. */
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }

    /* Create the timers that are used from the tick interrupt to test the timer
     * API functions that can be called from an ISR. */
    xISRAutoReloadTimer = xTimerCreate( "ISR AR",                        /* The text name given to the timer. */
                                        0xffff,                          /* The timer is not given a period yet - this will be done from the tick hook, but a period of 0 is invalid. */
                                        pdTRUE,                          /* This is an auto-reload timer. */
                                        ( void * ) NULL,                 /* The identifier is not required. */
                                        prvISRAutoReloadTimerCallback ); /* The callback that is executed when the timer expires. */

    xISROneShotTimer = xTimerCreate( "ISR OS",                           /* The text name given to the timer. */
                                     0xffff,                             /* The timer is not given a period yet - this will be done from the tick hook, but a period of 0 is invalid. */
                                     pdFALSE,                            /* This is a one-shot timer. */
                                     ( void * ) NULL,                    /* The identifier is not required. */
                                     prvISROneShotTimerCallback );       /* The callback that is executed when the timer expires. */

    if( ( xISRAutoReloadTimer == NULL ) || ( xISROneShotTimer == NULL ) )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }
}
/*-----------------------------------------------------------*/

static void prvTest2_CheckTaskAndTimersInitialState( void )
{
    uint8_t ucTimer;

    /* Ensure all the timers are in their expected initial state.  This depends
     * on the timer service task having a higher priority than this task.
     *
     * auto-reload timers 0 to ( configTIMER_QUEUE_LENGTH - 1 ) should now be active,
     * and auto-reload timer configTIMER_QUEUE_LENGTH should not yet be active (it
     * could not be started prior to the scheduler being started when it was
     * created). */
    for( ucTimer = 0; ucTimer < ( uint8_t ) configTIMER_QUEUE_LENGTH; ucTimer++ )
    {
        if( xTimerIsTimerActive( xAutoReloadTimers[ ucTimer ] ) == pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }

    if( xTimerIsTimerActive( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }
}
/*-----------------------------------------------------------*/

static void prvTest3_CheckAutoReloadExpireRates( void )
{
    uint8_t ucMaxAllowableValue, ucMinAllowableValue, ucTimer;
    TickType_t xBlockPeriod, xTimerPeriod, xExpectedNumber;
    UBaseType_t uxOriginalPriority;

    /* Check the auto-reload timers expire at the expected rates.  Do this at a
     * high priority for maximum accuracy.  This is ok as most of the time is spent
     * in the Blocked state. */
    uxOriginalPriority = uxTaskPriorityGet( NULL );
    vTaskPrioritySet( NULL, ( configMAX_PRIORITIES - 1 ) );

    /* Delaying for configTIMER_QUEUE_LENGTH * xHelper[ BASE_PERIOD ] ticks should allow
     * all the auto-reload timers to expire at least once. */
    xBlockPeriod = ( ( TickType_t ) configTIMER_QUEUE_LENGTH ) * xHelper[ BASE_PERIOD ];
    vTaskDelay( xBlockPeriod );

    /* Check that all the auto-reload timers have called their callback
     * function the expected number of times. */
    for( ucTimer = 0; ucTimer < ( uint8_t ) configTIMER_QUEUE_LENGTH; ucTimer++ )
    {
        /* The expected number of expires is equal to the block period divided
         * by the timer period. */
        xTimerPeriod = ( ( ( TickType_t ) ucTimer + ( TickType_t ) 1 ) * xHelper[ BASE_PERIOD ] );
        xExpectedNumber = xBlockPeriod / xTimerPeriod;

        ucMaxAllowableValue = ( ( uint8_t ) xExpectedNumber );
        ucMinAllowableValue = ( uint8_t ) ( ( uint8_t ) xExpectedNumber - ( uint8_t ) 1 ); /* Weird casting to try and please all compilers. */

        if( ( ucAutoReloadTimerCounters[ ucTimer ] < ucMinAllowableValue ) ||
            ( ucAutoReloadTimerCounters[ ucTimer ] > ucMaxAllowableValue )
            )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }

    /* Return to the original priority. */
    vTaskPrioritySet( NULL, uxOriginalPriority );

    if( xHelper[ TEST_STATUS ] == pdPASS )
    {
        /* No errors have been reported so increment the loop counter so the
         * check task knows this task is still running. */
        xHelper[ LOOP_COUNTER ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvTest4_CheckAutoReloadTimersCanBeStopped( void )
{
    uint8_t ucTimer;

    /* Check the auto-reload timers can be stopped correctly, and correctly
     * report their state. */

    /* Stop all the active timers. */
    for( ucTimer = 0; ucTimer < ( uint8_t ) configTIMER_QUEUE_LENGTH; ucTimer++ )
    {
        /* The timer has not been stopped yet! */
        if( xTimerIsTimerActive( xAutoReloadTimers[ ucTimer ] ) == pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        /* Now stop the timer.  This will appear to happen immediately to
         * this task because this task is running at a priority below the
         * timer service task. */
        xTimerStop( xAutoReloadTimers[ ucTimer ], tmrdemoDONT_BLOCK );

        /* The timer should now be inactive. */
        if( xTimerIsTimerActive( xAutoReloadTimers[ ucTimer ] ) != pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }

    taskENTER_CRITICAL();
    {
        /* The timer in array position configTIMER_QUEUE_LENGTH should not
         * be active.  The critical section is used to ensure the timer does
         * not call its callback between the next line running and the array
         * being cleared back to zero, as that would mask an error condition. */
        if( ucAutoReloadTimerCounters[ configTIMER_QUEUE_LENGTH ] != ( uint8_t ) 0 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        /* Clear the timer callback count. */
        memset( ( void * ) ucAutoReloadTimerCounters, 0, sizeof( ucAutoReloadTimerCounters ) );
    }
    taskEXIT_CRITICAL();

    /* The timers are now all inactive, so this time, after delaying, none
     * of the callback counters should have incremented. */
    vTaskDelay( ( ( TickType_t ) configTIMER_QUEUE_LENGTH ) * xHelper[ BASE_PERIOD ] );

    for( ucTimer = 0; ucTimer < ( uint8_t ) configTIMER_QUEUE_LENGTH; ucTimer++ )
    {
        if( ucAutoReloadTimerCounters[ ucTimer ] != ( uint8_t ) 0 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }

    if( xHelper[ TEST_STATUS ] == pdPASS )
    {
        /* No errors have been reported so increment the loop counter so
         * the check task knows this task is still running. */
        xHelper[ LOOP_COUNTER ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvTest5_CheckBasicOneShotTimerBehaviour( void )
{
    /* Check the one-shot timer only calls its callback once after it has been
     * started, and that it reports its state correctly. */

    /* The one-shot timer should not be active yet. */
    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    if( ucOneShotTimerCounter[ 0 ] != ( uint8_t ) 0 )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Start the one-shot timer and check that it reports its state correctly. */
    xTimerStart( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoDONT_BLOCK );

    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) == pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Delay for three times as long as the one-shot timer period, then check
     * to ensure it has only called its callback once, and is now not in the
     * active state. */
    vTaskDelay( tmrdemoONE_SHOT_TIMER_PERIOD * ( TickType_t ) 3 );

    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    if( ucOneShotTimerCounter[ 0 ] != ( uint8_t ) 1 )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }
    else
    {
        /* Reset the one-shot timer callback count. */
        ucOneShotTimerCounter[ 0 ] = ( uint8_t ) 0;
    }

    if( xHelper[ TEST_STATUS ] == pdPASS )
    {
        /* No errors have been reported so increment the loop counter so the
         * check task knows this task is still running. */
        xHelper[ LOOP_COUNTER ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvTest6_CheckAutoReloadResetBehaviour( void )
{
    uint8_t ucTimer;

    /* Check timer reset behaviour. */

    /* Restart the one-shot timer and check it reports its status correctly. */
    xTimerStart( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoDONT_BLOCK );

    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) == pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Restart one of the auto-reload timers and check that it reports its
     * status correctly. */
    xTimerStart( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ], tmrdemoDONT_BLOCK );

    if( xTimerIsTimerActive( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ] ) == pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    for( ucTimer = 0; ucTimer < tmrdemoNUM_TIMER_RESETS; ucTimer++ )
    {
        /* Delay for half as long as the one-shot timer period, then reset it.
         * It should never expire while this is done, so its callback count should
         * never increment. */
        vTaskDelay( tmrdemoONE_SHOT_TIMER_PERIOD / 2 );

        /* Check both running timers are still active, but have not called their
         * callback functions. */
        if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) == pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucOneShotTimerCounter[ 0 ] != ( uint8_t ) 0 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( xTimerIsTimerActive( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ] ) == pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucAutoReloadTimerCounters[ configTIMER_QUEUE_LENGTH - 1 ] != ( uint8_t ) 0 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        /* Reset both running timers. */
        xTimerReset( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoDONT_BLOCK );
        xTimerReset( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ], tmrdemoDONT_BLOCK );

        if( xHelper[ TEST_STATUS ] == pdPASS )
        {
            /* No errors have been reported so increment the loop counter so
             * the check task knows this task is still running. */
            xHelper[ LOOP_COUNTER ]++;
        }
    }

    /* Finally delay long enough for both running timers to expire. */
    vTaskDelay( ( ( TickType_t ) configTIMER_QUEUE_LENGTH ) * xHelper[ BASE_PERIOD ] );

    /* The timers were not reset during the above delay period so should now
     * both have called their callback functions. */
    if( ucOneShotTimerCounter[ 0 ] != ( uint8_t ) 1 )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    if( ucAutoReloadTimerCounters[ configTIMER_QUEUE_LENGTH - 1 ] == 0 )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* The one-shot timer should no longer be active, while the auto-reload
     * timer should still be active. */
    if( xTimerIsTimerActive( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ] ) == pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) == pdTRUE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Stop the auto-reload timer again. */
    xTimerStop( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ], tmrdemoDONT_BLOCK );

    if( xTimerIsTimerActive( xAutoReloadTimers[ configTIMER_QUEUE_LENGTH - 1 ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Clear the timer callback counts, ready for another iteration of these
     * tests. */
    ucAutoReloadTimerCounters[ configTIMER_QUEUE_LENGTH - 1 ] = ( uint8_t ) 0;
    ucOneShotTimerCounter[ 0 ] = ( uint8_t ) 0;

    if( xHelper[ TEST_STATUS ] == pdPASS )
    {
        /* No errors have been reported so increment the loop counter so the check
         * task knows this task is still running. */
        xHelper[ LOOP_COUNTER ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvTest7_CheckBacklogBehaviour( void )
{
    UBaseType_t uxOriginalPriority;

    /* Use the first auto-reload timer to test stopping a timer from a
     * backlogged callback. */

    /* The timer has not been started yet! */
    if( xTimerIsTimerActive( xAutoReloadTimers[ 0 ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Prompt the callback function to stop the timer. */
    ucIsStopNeededInTimerZeroCallback = ( uint8_t ) pdTRUE;

    /* Now start the timer.  This will appear to happen immediately to
     * this task because this task is running at a priority below the timer
     * service task.  Use a timer period of one tick so the call to
     * xTaskCatchUpTicks() below has minimal impact on other tests that might
     * be running. */
#define tmrdemoBACKLOG_TIMER_PERIOD    ( ( TickType_t ) 1 )
    xTimerChangePeriod( xAutoReloadTimers[ 0 ], tmrdemoBACKLOG_TIMER_PERIOD, tmrdemoDONT_BLOCK );

    /* The timer should now be active. */
    if( xTimerIsTimerActive( xAutoReloadTimers[ 0 ] ) == pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Arrange for the callback to execute late enough that it will execute
     * twice, back-to-back.  The timer must handle the stop request properly
     * in spite of the backlog of callbacks. */
#define tmrdemoEXPECTED_BACKLOG_EXPIRES    ( ( TickType_t ) 2 )
    xTaskCatchUpTicks( tmrdemoBACKLOG_TIMER_PERIOD * tmrdemoEXPECTED_BACKLOG_EXPIRES );

    /* The timer should now be inactive. */
    if( xTimerIsTimerActive( xAutoReloadTimers[ 0 ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Restore the standard timer period, and leave the timer inactive. */
    xTimerChangePeriod( xAutoReloadTimers[ 0 ], xHelper[ BASE_PERIOD ], tmrdemoDONT_BLOCK );
    xTimerStop( xAutoReloadTimers[ 0 ], tmrdemoDONT_BLOCK );

    /* Clear the reload count for the timer used in this test. */
    ucAutoReloadTimerCounters[ 0 ] = ( uint8_t ) 0;


    /* Verify a one-shot timer is marked as inactive if the timer task processes
     * the start or reset request after the expiration time has passed. */

    /* The timer has not been started yet! */
    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Use the timer period specific to backlogged timers because it reduces
     * the impact on other tests that might be running when xTaskCatchUpTicks()
     * creates the backlog, below. */
    xTimerChangePeriod( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoBACKLOG_TIMER_PERIOD, tmrdemoDONT_BLOCK );

    /* Temporarily give this task maximum priority so it can cause the timer
     * task to delay its processing of the reset request below. */
    uxOriginalPriority = uxTaskPriorityGet( NULL );
    vTaskPrioritySet( NULL, ( configMAX_PRIORITIES - 1 ) );

    /* Reset the timer.  The timer service won't process this request right
     * away as noted above. */
    xTimerReset( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoDONT_BLOCK );

    /* Cause the timer period to elapse without giving an opportunity for the
     * timer service task to process the reset request. */
    xTaskCatchUpTicks( tmrdemoBACKLOG_TIMER_PERIOD );

    /* Return this task to its original priority.  The timer service task will
     * process the reset request immediately.  The timer task must handle the reset
     * request as if it were processed at the time of the request even though in
     * this test the processing occurs after the intended expiration time. */
    vTaskPrioritySet( NULL, uxOriginalPriority );

    /* The timer should now be inactive. */
    if( xTimerIsTimerActive( xAutoReloadTimers[ ONE_SHOT_TIMER ] ) != pdFALSE )
    {
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }

    /* Restore the standard timer period, and leave the timer inactive. */
    xTimerChangePeriod( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoONE_SHOT_TIMER_PERIOD, tmrdemoDONT_BLOCK );
    xTimerStop( xAutoReloadTimers[ ONE_SHOT_TIMER ], tmrdemoDONT_BLOCK );

    /* Clear the counter for the timer used in this test. */
    ucOneShotTimerCounter[ 0 ] = ( uint8_t ) 0;

    if( xHelper[ TEST_STATUS ] == pdPASS )
    {
        /* No errors have been reported so increment the loop counter so the check
         * task knows this task is still running. */
        xHelper[ LOOP_COUNTER ]++;
    }
}
/*-----------------------------------------------------------*/

static void prvResetStartConditionsForNextIteration( void )
{
    uint8_t ucTimer;

    /* Start the timers again to start all the tests over again. */

    /* Start the timers again. */
    for( ucTimer = 0; ucTimer < ( uint8_t ) configTIMER_QUEUE_LENGTH; ucTimer++ )
    {
        /* The timer has not been started yet! */
        if( xTimerIsTimerActive( xAutoReloadTimers[ ucTimer ] ) != pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        /* Now start the timer.  This will appear to happen immediately to
         * this task because this task is running at a priority below the timer
         * service task. */
        xTimerStart( xAutoReloadTimers[ ucTimer ], tmrdemoDONT_BLOCK );

        /* The timer should now be active. */
        if( xTimerIsTimerActive( xAutoReloadTimers[ ucTimer ] ) == pdFALSE )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }

    if( xHelper[ TEST_STATUS ] == pdPASS )
    {
        /* No errors have been reported so increment the loop counter so the
         * check task knows this task is still running. */
        xHelper[ LOOP_COUNTER ]++;
    }
}
/*-----------------------------------------------------------*/

void vTimerPeriodicISRTests( void )
{
    static TickType_t uxTick = ( TickType_t ) -1;

    #if ( configTIMER_TASK_PRIORITY != ( configMAX_PRIORITIES - 1 ) )
        /* The timer service task is not the highest priority task, so it cannot
         * be assumed that timings will be exact.  Timers should never call their
         * callback before their expiry time, but a margin is permissible for calling
         * their callback after their expiry time.  If exact timing is required then
         * configTIMER_TASK_PRIORITY must be set to ensure the timer service task
         * is the highest priority task in the system.
         *
         * This function is called from the tick hook.  The tick hook is called
         * even when the scheduler is suspended.  Therefore it is possible that the
         * uxTick count maintained in this function is temporarily ahead of the tick
         * count maintained by the kernel.  When this is the case a message posted from
         * this function will assume a time stamp in advance of the real time stamp,
         * which can result in a timer being processed before this function expects it
         * to.  For example, if the kernel's tick count was 100, and uxTick was 102,
         * then this function will not expect the timer to have expired until the
         * kernel's tick count is (102 + xHelper[ BASE_PERIOD ]), whereas in reality the timer
         * will expire when the kernel's tick count is (100 + xHelper[ BASE_PERIOD ]).  For this
         * reason xMargin is used as an allowable margin for premature timer expires
         * as well as late timer expires. */
        #ifdef _WINDOWS_
            /* Windows is not real real time. */
            const TickType_t xMargin = 20;
        #else
            const TickType_t xMargin = 6;
        #endif /* _WINDOWS_ */
    #else
        #ifdef _WINDOWS_
            /* Windows is not real real time. */
            const TickType_t xMargin = 20;
        #else
            const TickType_t xMargin = 4;
        #endif /* _WINDOWS_ */
    #endif /* if ( configTIMER_TASK_PRIORITY != ( configMAX_PRIORITIES - 1 ) ) */


    uxTick++;

    if( uxTick == 0 )
    {
        /* The timers will have been created, but not started.  Start them now
         * by setting their period. */
        ucISRAutoReloadTimerCounter = 0;
        ucISROneShotTimerCounter = 0;

        /* It is possible that the timer task has not yet made room in the
         * timer queue.  If the timers cannot be started then reset uxTick so
         * another attempt is made later. */
        uxTick = ( TickType_t ) -1;

        /* Try starting first timer. */
        if( xTimerChangePeriodFromISR( xISRAutoReloadTimer, xHelper[ BASE_PERIOD ], NULL ) == pdPASS )
        {
            /* First timer was started, try starting the second timer. */
            if( xTimerChangePeriodFromISR( xISROneShotTimer, xHelper[ BASE_PERIOD ], NULL ) == pdPASS )
            {
                /* Both timers were started, so set the uxTick back to its
                 * proper value. */
                uxTick = 0;
            }
            else
            {
                /* Second timer could not be started, so stop the first one
                 * again. */
                xTimerStopFromISR( xISRAutoReloadTimer, NULL );
            }
        }
    }
    else if( uxTick == ( xHelper[ BASE_PERIOD ] - xMargin ) )
    {
        /* Neither timer should have expired yet. */
        if( ( ucISRAutoReloadTimerCounter != 0 ) || ( ucISROneShotTimerCounter != 0 ) )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( xHelper[ BASE_PERIOD ] + xMargin ) )
    {
        /* Both timers should now have expired once.  The auto-reload timer will
         * still be active, but the one-shot timer should now have stopped. */
        if( ( ucISRAutoReloadTimerCounter != 1 ) || ( ucISROneShotTimerCounter != 1 ) )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( ( 2 * xHelper[ BASE_PERIOD ] ) - xMargin ) )
    {
        /* The auto-reload timer will still be active, but the one-shot timer
         * should now have stopped - however, at this time neither of the timers
         * should have expired again since the last test. */
        if( ( ucISRAutoReloadTimerCounter != 1 ) || ( ucISROneShotTimerCounter != 1 ) )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( ( 2 * xHelper[ BASE_PERIOD ] ) + xMargin ) )
    {
        /* The auto-reload timer will still be active, but the one-shot timer
         * should now have stopped.  At this time the auto-reload timer should have
         * expired again, but the one-shot timer count should not have changed. */
        if( ucISRAutoReloadTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 1 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( ( 2 * xHelper[ BASE_PERIOD ] ) + ( xHelper[ BASE_PERIOD ] >> ( TickType_t ) 2U ) ) )
    {
        /* The auto-reload timer will still be active, but the one-shot timer
         * should now have stopped.  Again though, at this time, neither timer call
         * back should have been called since the last test. */
        if( ucISRAutoReloadTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 1 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( 3 * xHelper[ BASE_PERIOD ] ) )
    {
        /* Start the one-shot timer again. */
        xTimerStartFromISR( xISROneShotTimer, NULL );
    }
    else if( uxTick == ( ( 3 * xHelper[ BASE_PERIOD ] ) + xMargin ) )
    {
        /* The auto-reload timer and one-shot timer will be active.  At
         * this time the auto-reload timer should have expired again, but the one
         * shot timer count should not have changed yet. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 1 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        /* Now stop the auto-reload timer.  The one-shot timer was started
         * a few ticks ago. */
        xTimerStopFromISR( xISRAutoReloadTimer, NULL );
    }
    else if( uxTick == ( 4 * ( xHelper[ BASE_PERIOD ] - xMargin ) ) )
    {
        /* The auto-reload timer is now stopped, and the one-shot timer is
         * active, but at this time neither timer should have expired since the
         * last test. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 1 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( ( 4 * xHelper[ BASE_PERIOD ] ) + xMargin ) )
    {
        /* The auto-reload timer is now stopped, and the one-shot timer is
         * active.  The one-shot timer should have expired again, but the auto
         * reload timer should not have executed its callback. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( 8 * xHelper[ BASE_PERIOD ] ) )
    {
        /* The auto-reload timer is now stopped, and the one-shot timer has
         * already expired and then stopped itself.  Both callback counters should
         * not have incremented since the last test. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        /* Now reset the one-shot timer. */
        xTimerResetFromISR( xISROneShotTimer, NULL );
    }
    else if( uxTick == ( ( 9 * xHelper[ BASE_PERIOD ] ) - xMargin ) )
    {
        /* Only the one-shot timer should be running, but it should not have
         * expired since the last test.  Check the callback counters have not
         * incremented, then reset the one-shot timer again. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        xTimerResetFromISR( xISROneShotTimer, NULL );
    }
    else if( uxTick == ( ( 10 * xHelper[ BASE_PERIOD ] ) - ( 2 * xMargin ) ) )
    {
        /* Only the one-shot timer should be running, but it should not have
         * expired since the last test.  Check the callback counters have not
         * incremented, then reset the one-shot timer again. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        xTimerResetFromISR( xISROneShotTimer, NULL );
    }
    else if( uxTick == ( ( 11 * xHelper[ BASE_PERIOD ] ) - ( 3 * xMargin ) ) )
    {
        /* Only the one-shot timer should be running, but it should not have
         * expired since the last test.  Check the callback counters have not
         * incremented, then reset the one-shot timer once again. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 2 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        xTimerResetFromISR( xISROneShotTimer, NULL );
    }
    else if( uxTick == ( ( 12 * xHelper[ BASE_PERIOD ] ) - ( 2 * xMargin ) ) )
    {
        /* Only the one-shot timer should have been running and this time it
         * should have expired.  Check its callback count has been incremented.
         * The auto-reload timer is still not running so should still have the same
         * count value.  This time the one-shot timer is not reset so should not
         * restart from its expiry period again. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }
    }
    else if( uxTick == ( 15 * xHelper[ BASE_PERIOD ] ) )
    {
        /* Neither timer should be running now.  Check neither callback count
         * has incremented, then go back to the start to run these tests all
         * over again. */
        if( ucISRAutoReloadTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        if( ucISROneShotTimerCounter != 3 )
        {
            xHelper[ TEST_STATUS ] = pdFAIL;
            configASSERT( xHelper[ TEST_STATUS ] );
        }

        uxTick = ( TickType_t ) -1;
    }
}
/*-----------------------------------------------------------*/

/*** Timer callback functions are defined below here. ***/

static void prvAutoReloadTimerCallback( TimerHandle_t pxExpiredTimer )
{
    size_t uxTimerID;

    uxTimerID = ( size_t ) pvTimerGetTimerID( pxExpiredTimer );

    if( uxTimerID <= ( configTIMER_QUEUE_LENGTH + 1 ) )
    {
        ( ucAutoReloadTimerCounters[ uxTimerID ] )++;

        /* Stop timer ID 0 if requested. */
        if( ( uxTimerID == ( size_t ) 0 ) && ( ucIsStopNeededInTimerZeroCallback == ( uint8_t ) pdTRUE ) )
        {
            xTimerStop( pxExpiredTimer, tmrdemoDONT_BLOCK );
            ucIsStopNeededInTimerZeroCallback = ( uint8_t ) pdFALSE;
        }
    }
    else
    {
        /* The timer ID appears to be unexpected (invalid). */
        xHelper[ TEST_STATUS ] = pdFAIL;
        configASSERT( xHelper[ TEST_STATUS ] );
    }
}
/*-----------------------------------------------------------*/

static void prvOneShotTimerCallback( TimerHandle_t pxExpiredTimer )
{
/* A count is kept of the number of times this callback function is executed.
 * The count is stored as the timer's ID.  This is only done to test the
 * vTimerSetTimerID() function. */
    static size_t uxCallCount = 0;
    size_t uxLastCallCount;

    /* Obtain the timer's ID, which should be a count of the number of times
     * this callback function has been executed. */
    uxLastCallCount = ( size_t ) pvTimerGetTimerID( pxExpiredTimer );
    configASSERT( uxLastCallCount == uxCallCount );

    /* Increment the call count, then save it back as the timer's ID.  This is
     * only done to test the vTimerSetTimerID() API function. */
    uxLastCallCount++;
    vTimerSetTimerID( pxExpiredTimer, ( void * ) uxLastCallCount );
    uxCallCount++;

    ucOneShotTimerCounter[ 0 ]++;
}
/*-----------------------------------------------------------*/

static void prvISRAutoReloadTimerCallback( TimerHandle_t pxExpiredTimer )
{
    /* The parameter is not used in this case as only one timer uses this
     * callback function. */
    ( void ) pxExpiredTimer;

    ucISRAutoReloadTimerCounter++;
}
/*-----------------------------------------------------------*/

static void prvISROneShotTimerCallback( TimerHandle_t pxExpiredTimer )
{
    /* The parameter is not used in this case as only one timer uses this
     * callback function. */
    ( void ) pxExpiredTimer;

    ucISROneShotTimerCounter++;
}
/*-----------------------------------------------------------*/

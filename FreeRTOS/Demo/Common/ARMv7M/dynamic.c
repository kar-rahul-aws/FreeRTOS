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
 * The first test creates three tasks - two counter tasks (one continuous count
 * and one limited count) and one controller.  A "count" variable is shared
 * between all three tasks.  The two counter tasks should never be in a "ready"
 * state at the same time.  The controller task runs at the same priority as
 * the continuous count task, and at a lower priority than the limited count
 * task.
 *
 * One counter task loops indefinitely, incrementing the shared count variable
 * on each iteration.  To ensure it has exclusive access to the variable it
 * raises its priority above that of the controller task before each
 * increment, lowering it again to its original priority before starting the
 * next iteration.
 *
 * The other counter task increments the shared count variable on each
 * iteration of its loop until the count has reached a limit of 0xff - at
 * which point it suspends itself.  It will not start a new loop until the
 * controller task has made it "ready" again by calling vTaskResume().
 * This second counter task operates at a higher priority than controller
 * task so does not need to worry about mutual exclusion of the counter
 * variable.
 *
 * The controller task is in two sections.  The first section controls and
 * monitors the continuous count task.  When this section is operational the
 * limited count task is suspended.  Likewise, the second section controls
 * and monitors the limited count task.  When this section is operational the
 * continuous count task is suspended.
 *
 * In the first section the controller task first takes a copy of the shared
 * count variable.  To ensure mutual exclusion on the count variable it
 * suspends the continuous count task, resuming it again when the copy has been
 * taken.  The controller task then sleeps for a fixed period - during which
 * the continuous count task will execute and increment the shared variable.
 * When the controller task wakes it checks that the continuous count task
 * has executed by comparing the copy of the shared variable with its current
 * value.  This time, to ensure mutual exclusion, the scheduler itself is
 * suspended with a call to vTaskSuspendAll ().  This is for demonstration
 * purposes only and is not a recommended technique due to its inefficiency.
 *
 * After a fixed number of iterations the controller task suspends the
 * continuous count task, and moves on to its second section.
 *
 * At the start of the second section the shared variable is cleared to zero.
 * The limited count task is then woken from its suspension by a call to
 * vTaskResume ().  As this counter task operates at a higher priority than
 * the controller task the controller task should not run again until the
 * shared variable has been counted up to the limited value causing the counter
 * task to suspend itself.  The next line after vTaskResume () is therefore
 * a check on the shared variable to ensure everything is as expected.
 *
 *
 * The second test consists of a couple of very simple tasks that post onto a
 * queue while the scheduler is suspended.  This test was added to test parts
 * of the scheduler not exercised by the first test.
 *
 */

#include <stdlib.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Demo app include files. */
#include "dynamic.h"

/* Function that implements the "limited count" task as described above. */
static portTASK_FUNCTION_PROTO( vLimitedIncrementTask, pvParameters );

/* Function that implements the "continuous count" task as described above. */
static portTASK_FUNCTION_PROTO( vContinuousIncrementTask, pvParameters );

/* Function that implements the controller task as described above. */
static portTASK_FUNCTION_PROTO( vCounterControlTask, pvParameters );

static portTASK_FUNCTION_PROTO( vQueueReceiveWhenSuspendedTask, pvParameters );
static portTASK_FUNCTION_PROTO( vQueueSendWhenSuspendedTask, pvParameters );

/* Demo task specific constants. */
#ifndef priSUSPENDED_RX_TASK_STACK_SIZE
    #define priSUSPENDED_RX_TASK_STACK_SIZE    ( configMINIMAL_STACK_SIZE )
#endif
#define priSTACK_SIZE                          ( configMINIMAL_STACK_SIZE )
#define priSLEEP_TIME                          pdMS_TO_TICKS( 128 )
#define priLOOPS                               ( 5 )
#define priMAX_COUNT                           ( ( uint32_t ) 0xff )
#define priNO_BLOCK                            ( ( TickType_t ) 0 )
#define priSUSPENDED_QUEUE_LENGTH              ( 1 )


#define dynamicSHARED_MEM_SIZE_WORDS           ( 8 )
#define dynamicSHARED_MEM_SIZE_HALF_WORDS      ( 16 )
#define dynamicSHARED_MEM_SIZE_BYTES           ( 32 )

/*-----------------------------------------------------------*/

/* Handles to the two counter tasks.  These could be passed in as parameters
 * to the controller task to prevent them having to be file scope. */
#define CONTINUOUS_INCREMENT_TASK_IDX           ( 0 )
#define LIMITED_INCREMENT_TASK_IDX              ( 1 )
static TaskHandle_t xLocalTaskHandles[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

/* The shared counter variable.  This is passed in as a parameter to the two
 * counter variables for demonstration purposes. */
static uint32_t ulCounter[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* Variables used to check that the tasks are still operating without error.
 * Each complete iteration of the controller task increments this variable
 * provided no errors have been found.  The variable maintaining the same value
 * is therefore indication of an error. */
static volatile uint16_t usCheckVariable[ dynamicSHARED_MEM_SIZE_HALF_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { (uint16_t) 0 };
static volatile BaseType_t xSuspendedQueueSendError[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE };
static volatile BaseType_t xSuspendedQueueReceiveError[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE };

/* Queue used by the second test. */
QueueHandle_t xSuspendedTestQueue[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) );

/* The value the queue receive task expects to receive next.  This is file
 * scope so xAreDynamicPriorityTasksStillRunning() can ensure it is still
 * incrementing. */
static uint32_t ulExpectedValue[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
static uint32_t ulValueToSend[ dynamicSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( dynamicSHARED_MEM_SIZE_BYTES ) ) ) = { ( uint32_t ) 0 };

/*-----------------------------------------------------------*/

/*
 * Start the three tasks as described at the top of the file.
 * Note that the limited count task is given a higher priority.
 */
void vStartDynamicPriorityTasks( void )
{
static StackType_t xContinuousIncrementTaskStack[ priSTACK_SIZE ] __attribute__( ( aligned( priSTACK_SIZE * sizeof( StackType_t ) ) ) );
static StackType_t xLimitedIncrementTaskStack[ priSTACK_SIZE ] __attribute__( ( aligned( priSTACK_SIZE * sizeof( StackType_t ) ) ) );
static StackType_t xCounterControlTaskStack[ priSUSPENDED_RX_TASK_STACK_SIZE ] __attribute__( ( aligned( priSUSPENDED_RX_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
static StackType_t xQueueSendWhenSuspendedTaskStack[ priSTACK_SIZE ] __attribute__( ( aligned( priSTACK_SIZE * sizeof( StackType_t ) ) ) );
static StackType_t xQueueReceiveWhenSuspendedTaskStack[ priSUSPENDED_RX_TASK_STACK_SIZE ] __attribute__( ( aligned( priSUSPENDED_RX_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );

    xSuspendedTestQueue[ 0 ] = xQueueCreate( priSUSPENDED_QUEUE_LENGTH, sizeof( uint32_t ) );

    if( xSuspendedTestQueue[ 0 ] != NULL )
    {
        /* vQueueAddToRegistry() adds the queue to the queue registry, if one is
         * in use.  The queue registry is provided as a means for kernel aware
         * debuggers to locate queues and has no purpose if a kernel aware debugger
         * is not being used.  The call to vQueueAddToRegistry() will be removed
         * by the pre-processor if configQUEUE_REGISTRY_SIZE is not defined or is
         * defined to be less than 1. */
        vQueueAddToRegistry( xSuspendedTestQueue[ 0 ], "Suspended_Test_Queue" );

        TaskParameters_t xContinuousIncrementTask =
        {
            .pvTaskCode      = vContinuousIncrementTask,
            .pcName          = "CNT_INC",
            .usStackDepth    = priSTACK_SIZE,
            .pvParameters    = ( void * ) &ulCounter[ 0 ],
			/* Needs to be privileged because it calls privileged only APIs --> Set Priority */
            .uxPriority      = ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer  = xContinuousIncrementTaskStack,
            .xRegions        =  {
                                    { ( void * ) &( ulCounter[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
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
                                    { 0,                0,                    0                                                        }
                                }
        };
        TaskParameters_t xLimitedIncrementTask =
        {
            .pvTaskCode      = vLimitedIncrementTask,
            .pcName          = "LIM_INC",
            .usStackDepth    = priSTACK_SIZE,
            .pvParameters    = ( void * ) &ulCounter[ 0 ],
            .uxPriority      = tskIDLE_PRIORITY + 1,
            .puxStackBuffer  = xLimitedIncrementTaskStack,
            .xRegions        =  {
                                    { ( void * ) &( ulCounter[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
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
                                    { 0,                0,                    0                                                        }
                                }
        };
        TaskParameters_t xCounterControlTask =
        {
            .pvTaskCode      = vCounterControlTask,
            .pcName          = "C_CTRL",
            .usStackDepth    = priSUSPENDED_RX_TASK_STACK_SIZE,
            .pvParameters    = NULL,
			/* Needs to be privileged because it has to suspend another privileged task */
            .uxPriority      = ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer  = xCounterControlTaskStack,
            .xRegions        =  {
                                    { ( void * ) &( ulCounter[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] ), dynamicSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( usCheckVariable[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
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
                                    { 0,                0,                    0                                                        }
                                }
        };
        TaskParameters_t xQueueSendWhenSuspendedTask =
        {
            .pvTaskCode      = vQueueSendWhenSuspendedTask,
            .pcName          = "SUSP_TX",
            .usStackDepth    = priSTACK_SIZE,
            .pvParameters    = NULL,
			/* Needs to be privileged because it calls privileged only APIs --> TaskSuspendAll() */
            .uxPriority      = ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer  = xQueueSendWhenSuspendedTaskStack,
            .xRegions        =  {
                                    { ( void * ) &( xSuspendedTestQueue[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xSuspendedQueueSendError[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( ulValueToSend[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
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
                                    { 0,                0,                    0                                                        }
                                }
        };
        TaskParameters_t xQueueReceiveWhenSuspendedTask =
        {
            .pvTaskCode      = vQueueReceiveWhenSuspendedTask,
            .pcName          = "SUSP_RX",
            .usStackDepth    = priSUSPENDED_RX_TASK_STACK_SIZE,
            .pvParameters    = NULL,
			/* Needs to be privileged because it calls privileged only APIs --> TaskSuspendAll() */
            .uxPriority      = ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer  = xQueueReceiveWhenSuspendedTaskStack,
            .xRegions        =    {
                                    { ( void * ) &( xSuspendedTestQueue[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( xSuspendedQueueReceiveError[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { ( void * ) &( ulExpectedValue[ 0 ] ), dynamicSHARED_MEM_SIZE_BYTES,
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
                                    { 0,                0,                    0                                                        }
                                }
        };

        xTaskCreateRestricted( &( xContinuousIncrementTask ), &( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] ) );
        xTaskCreateRestricted( &( xLimitedIncrementTask ), &( xLocalTaskHandles[ LIMITED_INCREMENT_TASK_IDX ] ) );
        xTaskCreateRestricted( &( xCounterControlTask ), NULL );
        xTaskCreateRestricted( &( xQueueSendWhenSuspendedTask ), NULL );
        xTaskCreateRestricted( &( xQueueReceiveWhenSuspendedTask ), NULL );
    }
}
/*-----------------------------------------------------------*/

/*
 * Just loops around incrementing the shared variable until the limit has been
 * reached.  Once the limit has been reached it suspends itself.
 */
static portTASK_FUNCTION( vLimitedIncrementTask, pvParameters )
{
    volatile uint32_t * pulCounter;

    /* Take a pointer to the shared variable from the parameters passed into
     * the task. */
    pulCounter = ( volatile uint32_t * ) pvParameters;

    /* This will run before the control task, so the first thing it does is
     * suspend - the control task will resume it when ready. */
    vTaskSuspend( NULL );

    for( ; ; )
    {
        /* Just count up to a value then suspend. */
        ( *pulCounter )++;

        if( *pulCounter >= priMAX_COUNT )
        {
            vTaskSuspend( NULL );
        }
    }
}
/*-----------------------------------------------------------*/

/*
 * Just keep counting the shared variable up.  The control task will suspend
 * this task when it wants.
 */
static portTASK_FUNCTION( vContinuousIncrementTask, pvParameters )
{
    volatile uint32_t * pulCounter;
    UBaseType_t uxOurPriority;

    /* Take a pointer to the shared variable from the parameters passed into
     * the task. */
    pulCounter = ( volatile uint32_t * ) pvParameters;

    /* Query our priority so we can raise it when exclusive access to the
     * shared variable is required. */
    uxOurPriority = uxTaskPriorityGet( NULL );

    for( ; ; )
    {
        /* Raise the priority above the controller task to ensure a context
         * switch does not occur while the variable is being accessed. */
        vTaskPrioritySet( NULL, uxOurPriority + 1 );
        {
            configASSERT( ( uxTaskPriorityGet( NULL ) == ( uxOurPriority + 1 ) ) );
            ( *pulCounter )++;
        }
        vTaskPrioritySet( NULL, uxOurPriority );

        #if ( configUSE_PREEMPTION == 0 )
            taskYIELD();
        #endif

        configASSERT( ( uxTaskPriorityGet( NULL ) == uxOurPriority ) );
    }
}
/*-----------------------------------------------------------*/

/*
 * Controller task as described above.
 */
static portTASK_FUNCTION( vCounterControlTask, pvParameters )
{
    uint32_t ulLastCounter;
    short sLoops;
    short sError = pdFALSE;

    /* Just to stop warning messages. */
    ( void ) pvParameters;

    for( ; ; )
    {
        /* Start with the counter at zero. */
        ulCounter[ 0 ] = ( uint32_t ) 0;

        /* First section : */

        /* Check the continuous count task is running. */
        for( sLoops = 0; sLoops < priLOOPS; sLoops++ )
        {
            /* Suspend the continuous count task so we can take a mirror of the
             * shared variable without risk of corruption.  This is not really
             * needed as the other task raises its priority above this task's
             * priority. */
            vTaskSuspend( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] );
            {
                #if ( INCLUDE_eTaskGetState == 1 )
                {
                    configASSERT( eTaskGetState( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] ) == eSuspended );
                }
                #endif /* INCLUDE_eTaskGetState */

                ulLastCounter = ulCounter[ 0 ];
            }
            vTaskResume( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] );

            #if ( configUSE_PREEMPTION == 0 )
                taskYIELD();
            #endif

            #if ( INCLUDE_eTaskGetState == 1 )
            {
                configASSERT( eTaskGetState( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] ) == eReady );
            }
            #endif /* INCLUDE_eTaskGetState */

            /* Now delay to ensure the other task has processor time. */
            vTaskDelay( priSLEEP_TIME );

            /* Check the shared variable again.  This time to ensure mutual
             * exclusion the whole scheduler will be locked.  This is just for
             * demo purposes! */
            vTaskSuspendAll();
            {
                if( ulLastCounter == ulCounter[ 0 ] )
                {
                    /* The shared variable has not changed.  There is a problem
                     * with the continuous count task so flag an error. */
                    sError = pdTRUE;
                }
            }
            xTaskResumeAll();
        }

        /* Second section: */

        /* Suspend the continuous counter task so it stops accessing the shared
         * variable. */
        vTaskSuspend( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] );

        /* Reset the variable. */
        ulCounter[ 0 ] = ( uint32_t ) 0;

        #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xLocalTaskHandles[ LIMITED_INCREMENT_TASK_IDX ] ) == eSuspended );
        }
        #endif /* INCLUDE_eTaskGetState */

        /* Resume the limited count task which has a higher priority than us.
         * We should therefore not return from this call until the limited count
         * task has suspended itself with a known value in the counter variable. */
        vTaskResume( xLocalTaskHandles[ LIMITED_INCREMENT_TASK_IDX ] );

        #if ( configUSE_PREEMPTION == 0 )
            taskYIELD();
        #endif

        /* This task should not run again until xLimitedIncrementHandle has
         * suspended itself. */
        #if ( INCLUDE_eTaskGetState == 1 )
        {
            configASSERT( eTaskGetState( xLocalTaskHandles[ LIMITED_INCREMENT_TASK_IDX ] ) == eSuspended );
        }
        #endif /* INCLUDE_eTaskGetState */

        /* Does the counter variable have the expected value? */
        if( ulCounter[ 0 ] != priMAX_COUNT )
        {
            sError = pdTRUE;
        }

        if( sError == pdFALSE )
        {
            /* If no errors have occurred then increment the check variable. */
            portENTER_CRITICAL();
            usCheckVariable[ 0 ]++;
            portEXIT_CRITICAL();
        }

        /* Resume the continuous count task and do it all again. */
        vTaskResume( xLocalTaskHandles[ CONTINUOUS_INCREMENT_TASK_IDX ] );

        #if ( configUSE_PREEMPTION == 0 )
            taskYIELD();
        #endif
    }
}
/*-----------------------------------------------------------*/

static portTASK_FUNCTION( vQueueSendWhenSuspendedTask, pvParameters )
{

    /* Just to stop warning messages. */
    ( void ) pvParameters;

    for( ; ; )
    {
        vTaskSuspendAll();
        {
            /* We must not block while the scheduler is suspended! */
            if( xQueueSend( xSuspendedTestQueue[ 0 ], ( void * ) &ulValueToSend[ 0 ], priNO_BLOCK ) != pdTRUE )
            {
                xSuspendedQueueSendError[ 0 ] = pdTRUE;
            }
        }
        xTaskResumeAll();

        vTaskDelay( priSLEEP_TIME );

        ++ulValueToSend[ 0 ];
    }
}
/*-----------------------------------------------------------*/

static portTASK_FUNCTION( vQueueReceiveWhenSuspendedTask, pvParameters )
{
    uint32_t ulReceivedValue;
    BaseType_t xGotValue;

    /* Just to stop warning messages. */
    ( void ) pvParameters;

    for( ; ; )
    {
        do
        {
            /* Suspending the scheduler here is fairly pointless and
             * undesirable for a normal application.  It is done here purely
             * to test the scheduler.  The inner xTaskResumeAll() should
             * never return pdTRUE as the scheduler is still locked by the
             * outer call. */
            vTaskSuspendAll();
            {
                vTaskSuspendAll();
                {
                    xGotValue = xQueueReceive( xSuspendedTestQueue[ 0 ], ( void * ) &ulReceivedValue, priNO_BLOCK );
                }

                if( xTaskResumeAll() != pdFALSE )
                {
                    xSuspendedQueueReceiveError[ 0 ] = pdTRUE;
                }
            }
            xTaskResumeAll();

            #if configUSE_PREEMPTION == 0
            {
                taskYIELD();
            }
            #endif
        } while( xGotValue == pdFALSE );

        if( ulReceivedValue != ulExpectedValue[ 0 ] )
        {
            xSuspendedQueueReceiveError[ 0 ] = pdTRUE;
        }

        if( xSuspendedQueueReceiveError[ 0 ] != pdTRUE )
        {
            /* Only increment the variable if an error has not occurred.  This
             * allows xAreDynamicPriorityTasksStillRunning() to check for stalled
             * tasks as well as explicit errors. */
            ++ulExpectedValue[ 0 ];
        }
    }
}
/*-----------------------------------------------------------*/

/* Called to check that all the created tasks are still running without error. */
BaseType_t xAreDynamicPriorityTasksStillRunning( void )
{
/* Keep a history of the check variables so we know if it has been incremented
 * since the last call. */
    static uint16_t usLastTaskCheck = ( uint16_t ) 0;
    static uint32_t ulLastExpectedValue = ( uint32_t ) 0U;
    BaseType_t xReturn = pdTRUE;

    /* Check the tasks are still running by ensuring the check variable
     * is still incrementing. */

    if( usCheckVariable[ 0 ] == usLastTaskCheck )
    {
        /* The check has not incremented so an error exists. */
        xReturn = pdFALSE;
    }

    if( ulExpectedValue[ 0 ] == ulLastExpectedValue )
    {
        /* The value being received by the queue receive task has not
         * incremented so an error exists. */
        xReturn = pdFALSE;
    }

    if( xSuspendedQueueSendError[ 0 ] == pdTRUE )
    {
        xReturn = pdFALSE;
    }

    if( xSuspendedQueueReceiveError[ 0 ] == pdTRUE )
    {
        xReturn = pdFALSE;
    }

    usLastTaskCheck = usCheckVariable[ 0 ];
    ulLastExpectedValue = ulExpectedValue[ 0 ];

    return xReturn;
}

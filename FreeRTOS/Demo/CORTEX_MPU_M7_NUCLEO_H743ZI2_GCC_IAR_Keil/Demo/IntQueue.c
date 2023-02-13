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
 * This file defines one of the more complex set of demo/test tasks.  They are
 * designed to stress test the queue implementation though pseudo simultaneous
 * multiple reads and multiple writes from both tasks of varying priority and
 * interrupts.  The interrupts are prioritised such to ensure that nesting
 * occurs (for those ports that support it).
 *
 * The test ensures that, while being accessed from three tasks and two
 * interrupts, all the data sent to the queues is also received from
 * the same queue, and that no duplicate items are either sent or received.
 * The tests also ensure that a low priority task is never able to successfully
 * read from or write to a queue when a task of higher priority is attempting
 * the same operation.
 */

/* Standard includes. */
#include <string.h>

/* SafeRTOS includes. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Demo app includes. */
#include "IntQueue.h"
#include "IntQueueTimer.h"

#define intqSHARED_MEM_SIZE_WORDS               ( 8 )
#define intqSHARED_MEM_SIZE_HALF_WORDS          ( 16 )
#define intqSHARED_MEM_SIZE_BYTES               ( 32 )
#define intqSHARED_MEM_SIZE_VALUES_TO_LOG       ( 256 )

#if ( INCLUDE_eTaskGetState != 1 )
    #error INCLUDE_eTaskGetState must be set to 1 in FreeRTOSConfig.h to use this demo file.
#endif

/* Priorities used by test tasks. */
#ifndef intqHIGHER_PRIORITY
    #define intqHIGHER_PRIORITY          ( configMAX_PRIORITIES - 2 )
#endif
#define intqLOWER_PRIORITY               ( tskIDLE_PRIORITY )

/* The number of values to send/receive before checking that all values were
 * processed as expected. */
#define intqNUM_VALUES_TO_LOG            ( 200 )
#define intqSHORT_DELAY                  ( 140 )

/* The value by which the value being sent to or received from a queue should
 * increment past intqNUM_VALUES_TO_LOG before we check that all values have been
 * sent/received correctly.  This is done to ensure that all tasks and interrupts
 * accessing the queue have completed their accesses with the
 * intqNUM_VALUES_TO_LOG range. */
#define intqVALUE_OVERRUN                ( 50 )

/* The delay used by the polling task.  A short delay is used for code
 * coverage. */
#define intqONE_TICK_DELAY               ( 1 )

/* Each task and interrupt is given a unique identifier.  This value is used to
 * identify which task sent or received each value.  The identifier is also used
 * to distinguish between two tasks that are running the same task function. */
#define intqHIGH_PRIORITY_TASK1          ( ( UBaseType_t ) 1 )
#define intqHIGH_PRIORITY_TASK2          ( ( UBaseType_t ) 2 )
#define intqLOW_PRIORITY_TASK            ( ( UBaseType_t ) 3 )
#define intqFIRST_INTERRUPT              ( ( UBaseType_t ) 4 )
#define intqSECOND_INTERRUPT             ( ( UBaseType_t ) 5 )
#define intqQUEUE_LENGTH                 ( ( UBaseType_t ) 10 )

/* At least intqMIN_ACCEPTABLE_TASK_COUNT values should be sent to/received
 * from each queue by each task, otherwise an error is detected. */
#define intqMIN_ACCEPTABLE_TASK_COUNT    ( 5 )

/* Send the next value to the queue that is normally empty.  This is called
 * from within the interrupts. */
#define timerNORMALLY_EMPTY_TX()                                                                                                          \
    if( xQueueIsQueueFullFromISR( xNormallyEmptyQueue[ 0 ] ) != pdTRUE )                                                                  \
    {                                                                                                                                     \
        UBaseType_t uxSavedInterruptStatus;                                                                                               \
        uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();                                                                       \
        {                                                                                                                                 \
            uxValueForNormallyEmptyQueue[ 0 ]++;                                                                                          \
            if( xQueueSendFromISR( xNormallyEmptyQueue[ 0 ], ( void * ) &uxValueForNormallyEmptyQueue[ 0 ], &xHigherPriorityTaskWoken ) != pdPASS ) \
            {                                                                                                                             \
                uxValueForNormallyEmptyQueue[ 0 ]--;                                                                                      \
            }                                                                                                                             \
        }                                                                                                                                 \
        portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );                                                                      \
    }                                                                                                                                     \


/* Send the next value to the queue that is normally full.  This is called
 * from within the interrupts. */
#define timerNORMALLY_FULL_TX()                                                                                                         \
    if( xQueueIsQueueFullFromISR( xNormallyFullQueue[ 0 ] ) != pdTRUE )                                                                 \
    {                                                                                                                                   \
        UBaseType_t uxSavedInterruptStatus;                                                                                             \
        uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();                                                                     \
        {                                                                                                                               \
            uxValueForNormallyFullQueue[ 0 ]++;                                                                                         \
            if( xQueueSendFromISR( xNormallyFullQueue[ 0 ], ( void * ) &uxValueForNormallyFullQueue[ 0 ], &xHigherPriorityTaskWoken ) != pdPASS ) \
            {                                                                                                                           \
                uxValueForNormallyFullQueue[ 0 ]--;                                                                                     \
            }                                                                                                                           \
        }                                                                                                                               \
        portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );                                                                    \
    }                                                                                                                                   \


/* Receive a value from the normally empty queue.  This is called from within
 * an interrupt. */
#define timerNORMALLY_EMPTY_RX()                                                                         \
    if( xQueueReceiveFromISR( xNormallyEmptyQueue[ 0 ], &uxRxedValue, &xHigherPriorityTaskWoken ) != pdPASS ) \
    {                                                                                                    \
        prvQueueAccessLogError( __LINE__ );                                                              \
    }                                                                                                    \
    else                                                                                                 \
    {                                                                                                    \
        prvRecordValue_NormallyEmpty( uxRxedValue, intqSECOND_INTERRUPT );                               \
    }

/* Receive a value from the normally full queue.  This is called from within
 * an interrupt. */
#define timerNORMALLY_FULL_RX()                                                                         \
    if( xQueueReceiveFromISR( xNormallyFullQueue[ 0 ], &uxRxedValue, &xHigherPriorityTaskWoken ) == pdPASS ) \
    {                                                                                                   \
        prvRecordValue_NormallyFull( uxRxedValue, intqSECOND_INTERRUPT );                               \
    }                                                                                                   \


/*-----------------------------------------------------------*/

/* The two queues used by the test. */
static QueueHandle_t xNormallyEmptyQueue[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };
static QueueHandle_t xNormallyFullQueue[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

/* Variables used to detect a stall in one of the tasks. */
static volatile UBaseType_t uxHighPriorityLoops1[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
static volatile UBaseType_t uxHighPriorityLoops2[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
static volatile UBaseType_t uxLowPriorityLoops1[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
static volatile UBaseType_t uxLowPriorityLoops2[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* Any unexpected behaviour sets xErrorStatus to fail and log the line that
 * caused the error in xErrorLine. */
BaseType_t xErrorStatus[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { pdPASS };
static volatile UBaseType_t xErrorLine[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { ( UBaseType_t ) 0 };

/* Used for sequencing between tasks. */
static BaseType_t xWasSuspended[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { pdFALSE };

/* The values that are sent to the queues.  An incremented value is sent each
 * time to each queue. */
static volatile UBaseType_t uxValueForNormallyEmptyQueue[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
static volatile UBaseType_t uxValueForNormallyFullQueue[ intqSHARED_MEM_SIZE_WORDS ] __attribute__ ( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* A handle to some of the tasks is required so they can be suspended/resumed. */
#define LOW_PRIO_EMPTY_TASK_IDX                 ( 0 )
#define LOW_PRIO_FULL_TASK_IDX                  ( 1 )
#define FIRST_HIGH_PRIO_EMPTY_TASK_IDX          ( 2 )
#define SECOND_HIGH_PRIO_EMPTY_TASK_IDX         ( 3 )
#define FIRST_HIGH_PRIO_FULL_TASK_IDX           ( 4 )
#define SECOND_HIGH_PRIO_FULL_TASK_IDX          ( 5 )

static TaskHandle_t xLocalTaskHandles[ intqSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( intqSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

/* When a value is received in a queue the value is ticked off in the array
 * the array position of the value is set to a the identifier of the task or
 * interrupt that accessed the queue.  This way missing or duplicate values can be
 * detected. */
static uint8_t ucNormallyEmptyReceivedValues[ intqSHARED_MEM_SIZE_VALUES_TO_LOG ] __attribute__( ( aligned( intqSHARED_MEM_SIZE_VALUES_TO_LOG ) ) ) = { 0 };
static uint8_t ucNormallyFullReceivedValues[ intqSHARED_MEM_SIZE_VALUES_TO_LOG ] __attribute__( ( aligned( intqSHARED_MEM_SIZE_VALUES_TO_LOG ) ) ) = { 0 };

/* The test tasks themselves. */
static void prvLowerPriorityNormallyEmptyTask( void * pvParameters );
static void prvLowerPriorityNormallyFullTask( void * pvParameters );
static void prvHigherPriorityNormallyEmptyTask( void * pvParameters );
static void prv1stHigherPriorityNormallyFullTask( void * pvParameters );
static void prv2ndHigherPriorityNormallyFullTask( void * pvParameters );

/* Used to mark the positions within the ucNormallyEmptyReceivedValues and
 * ucNormallyFullReceivedValues arrays, while checking for duplicates. */
static void prvRecordValue_NormallyEmpty( UBaseType_t uxValue,
                                          UBaseType_t uxSource );
static void prvRecordValue_NormallyFull( UBaseType_t uxValue,
                                         UBaseType_t uxSource );

/* Logs the line on which an error occurred. */
static void prvQueueAccessLogError( UBaseType_t uxLine );
/*-----------------------------------------------------------*/

void vStartInterruptQueueTasks( void )
{
    /* Start the test tasks. */

    static StackType_t x1stHigherPriorityNormallyEmptyTaskStack[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t x2ndHigherPriorityNormallyEmptyTaskStack[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xLowerPriorityNormallyEmptyTaskStack[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t x1stHigherPriorityNormallyFullTaskStack[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t x2ndHigherPriorityNormallyFullTaskStack[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xLowerPriorityNormallyFullTaskStack[ configMINIMAL_STACK_SIZE ]__attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );

    TaskParameters_t x1stHigherPriorityNormallyEmptyTaskParameters =
    {
        .pvTaskCode      = prvHigherPriorityNormallyEmptyTask,
        .pcName          = "H1QRx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = ( void * ) intqHIGH_PRIORITY_TASK1,
        .uxPriority      = intqHIGHER_PRIORITY,
        .puxStackBuffer  = x1stHigherPriorityNormallyEmptyTaskStack,
        .xRegions        =  {
                                { ( void * ) &( xNormallyEmptyQueue[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorStatus[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorLine[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( ucNormallyEmptyReceivedValues[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxValueForNormallyEmptyQueue[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xLocalTaskHandles[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxHighPriorityLoops1[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        }
                            }
    };

    TaskParameters_t x2ndHigherPriorityNormallyEmptyTaskParameters =
    {
        .pvTaskCode      = prvHigherPriorityNormallyEmptyTask,
        .pcName          = "H2QRx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = ( void * ) intqHIGH_PRIORITY_TASK2,
        .uxPriority      = intqHIGHER_PRIORITY,
        .puxStackBuffer  = x2ndHigherPriorityNormallyEmptyTaskStack,
        .xRegions        =  {
                                { ( void * ) &( xNormallyEmptyQueue[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorStatus[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorLine[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( ucNormallyEmptyReceivedValues[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxValueForNormallyEmptyQueue[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xLocalTaskHandles[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxHighPriorityLoops1[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        }
                            }
    };
    TaskParameters_t xLowerPriorityNormallyEmptyTaskParameters =
    {
        .pvTaskCode      = prvLowerPriorityNormallyEmptyTask,
        .pcName          = "L1QRx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = NULL,
        .uxPriority      = intqLOWER_PRIORITY,
        .puxStackBuffer  = xLowerPriorityNormallyEmptyTaskStack,
        .xRegions        =  {
                                { ( void * ) &( xNormallyEmptyQueue[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xLocalTaskHandles[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxLowPriorityLoops1[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxValueForNormallyEmptyQueue[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( ucNormallyEmptyReceivedValues[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        }
                            }
    };
    TaskParameters_t x1stHigherPriorityNormallyFullTaskParameters =
    {
        .pvTaskCode      = prv1stHigherPriorityNormallyFullTask,
        .pcName          = "H1QTx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = ( void * ) intqHIGH_PRIORITY_TASK1,
        .uxPriority      = intqHIGHER_PRIORITY,
        .puxStackBuffer  = x1stHigherPriorityNormallyFullTaskStack,
        .xRegions        =  {
                                { ( void * ) &( uxValueForNormallyFullQueue[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xNormallyFullQueue[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xLocalTaskHandles[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xWasSuspended[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( ucNormallyFullReceivedValues[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxHighPriorityLoops2[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorStatus[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorLine[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        },
                                { 0,                0,                    0                                                        }
                            }
    };
    TaskParameters_t x2ndHigherPriorityNormallyFullTaskParameters =
    {
        .pvTaskCode      = prv2ndHigherPriorityNormallyFullTask,
        .pcName          = "H2QTx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = ( void * ) intqHIGH_PRIORITY_TASK2,
        .uxPriority      = intqHIGHER_PRIORITY,
        .puxStackBuffer  = x2ndHigherPriorityNormallyFullTaskStack,
        .xRegions        =  {
                                { ( void * ) &( uxValueForNormallyFullQueue[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xNormallyFullQueue[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xWasSuspended[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxValueForNormallyFullQueue[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
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

    TaskParameters_t xLowerPriorityNormallyFullTaskParameters =
    {
        .pvTaskCode      = prvLowerPriorityNormallyFullTask,
        .pcName          = "L2QRx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = NULL,
        .uxPriority      = intqLOWER_PRIORITY,
        .puxStackBuffer  = xLowerPriorityNormallyFullTaskStack,
        .xRegions        =  {
                                { ( void * ) &( xNormallyFullQueue[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xLocalTaskHandles[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( uxLowPriorityLoops2[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( ucNormallyFullReceivedValues[ 0 ] ), intqSHARED_MEM_SIZE_VALUES_TO_LOG,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorStatus[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
                                  ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                    ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                },
                                { ( void * ) &( xErrorLine[ 0 ] ), intqSHARED_MEM_SIZE_BYTES,
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

    xTaskCreateRestricted( &( x1stHigherPriorityNormallyEmptyTaskParameters ), &( xLocalTaskHandles[ FIRST_HIGH_PRIO_EMPTY_TASK_IDX ] ) );
    xTaskCreateRestricted( &( x2ndHigherPriorityNormallyEmptyTaskParameters ), &( xLocalTaskHandles[ SECOND_HIGH_PRIO_EMPTY_TASK_IDX ] ) );
    xTaskCreateRestricted( &( xLowerPriorityNormallyEmptyTaskParameters ), &( xLocalTaskHandles[ LOW_PRIO_EMPTY_TASK_IDX ] ) );
    xTaskCreateRestricted( &( x1stHigherPriorityNormallyFullTaskParameters ), &( xLocalTaskHandles[ FIRST_HIGH_PRIO_FULL_TASK_IDX ] ) );
    xTaskCreateRestricted( &( x2ndHigherPriorityNormallyFullTaskParameters ), &( xLocalTaskHandles[ SECOND_HIGH_PRIO_FULL_TASK_IDX ] ) );
    xTaskCreateRestricted( &( xLowerPriorityNormallyFullTaskParameters ), &( xLocalTaskHandles[ LOW_PRIO_FULL_TASK_IDX ] ) );

    /* Create the queues that are accessed by multiple tasks and multiple
     * interrupts. */
    xNormallyFullQueue[ 0 ] = xQueueCreate( intqQUEUE_LENGTH, ( UBaseType_t ) sizeof( UBaseType_t ) );
    xNormallyEmptyQueue[ 0 ] = xQueueCreate( intqQUEUE_LENGTH, ( UBaseType_t ) sizeof( UBaseType_t ) );

    /* vQueueAddToRegistry() adds the queue to the queue registry, if one is
     * in use.  The queue registry is provided as a means for kernel aware
     * debuggers to locate queues and has no purpose if a kernel aware debugger
     * is not being used.  The call to vQueueAddToRegistry() will be removed
     * by the pre-processor if configQUEUE_REGISTRY_SIZE is not defined or is
     * defined to be less than 1. */
    vQueueAddToRegistry( xNormallyFullQueue[ 0 ], "NormallyFull" );
    vQueueAddToRegistry( xNormallyEmptyQueue[ 0 ], "NormallyEmpty" );
}
/*-----------------------------------------------------------*/

static void prvRecordValue_NormallyFull( UBaseType_t uxValue,
                                         UBaseType_t uxSource )
{
    if( uxValue < intqNUM_VALUES_TO_LOG )
    {
        /* We don't expect to receive the same value twice, so if the value
         * has already been marked as received an error has occurred. */
        if( ucNormallyFullReceivedValues[ uxValue ] != 0x00 )
        {
            prvQueueAccessLogError( __LINE__ );
        }

        /* Log that this value has been received. */
        ucNormallyFullReceivedValues[ uxValue ] = ( uint8_t ) uxSource;
    }
}
/*-----------------------------------------------------------*/

static void prvRecordValue_NormallyEmpty( UBaseType_t uxValue,
                                          UBaseType_t uxSource )
{
    if( uxValue < intqNUM_VALUES_TO_LOG )
    {
        /* We don't expect to receive the same value twice, so if the value
         * has already been marked as received an error has occurred. */
        if( ucNormallyEmptyReceivedValues[ uxValue ] != 0x00 )
        {
            prvQueueAccessLogError( __LINE__ );
        }

        /* Log that this value has been received. */
        ucNormallyEmptyReceivedValues[ uxValue ] = ( uint8_t ) uxSource;
    }
}
/*-----------------------------------------------------------*/

static void prvQueueAccessLogError( UBaseType_t uxLine )
{
    /* Latch the line number that caused the error. */
    xErrorLine[ 0 ] = uxLine;
    xErrorStatus[ 0 ] = pdFAIL;
}
/*-----------------------------------------------------------*/

static void prvHigherPriorityNormallyEmptyTask( void * pvParameters )
{
    UBaseType_t uxRxed, ux, uxTask1, uxTask2, uxInterrupts, uxErrorCount1 = 0, uxErrorCount2 = 0;

    /* The timer should not be started until after the scheduler has started.
     * More than one task is running this code so we check the parameter value
     * to determine which task should start the timer. */
    if( ( UBaseType_t ) pvParameters == intqHIGH_PRIORITY_TASK1 )
    {
        vInitialiseTimerForIntQueueTest();
    }

    for( ; ; )
    {
        /* Block waiting to receive a value from the normally empty queue.
         * Interrupts will write to the queue so we should receive a value. */
        if( xQueueReceive( xNormallyEmptyQueue[ 0 ], &uxRxed, intqSHORT_DELAY ) != pdPASS )
        {
            prvQueueAccessLogError( __LINE__ );
        }
        else
        {
            /* Note which value was received so we can check all expected
             * values are received and no values are duplicated. */
            prvRecordValue_NormallyEmpty( uxRxed, ( UBaseType_t ) pvParameters );
        }

        /* Ensure the other task running this code gets a chance to execute. */
        taskYIELD();

        if( ( UBaseType_t ) pvParameters == intqHIGH_PRIORITY_TASK1 )
        {
            /* Have we received all the expected values? */
            if( uxValueForNormallyEmptyQueue[ 0 ] > ( intqNUM_VALUES_TO_LOG + intqVALUE_OVERRUN ) )
            {
                vTaskSuspend( xLocalTaskHandles[ SECOND_HIGH_PRIO_EMPTY_TASK_IDX ] );

                uxTask1 = 0;
                uxTask2 = 0;
                uxInterrupts = 0;

                /* Loop through the array, checking that both tasks have
                 * placed values into the array, and that no values are missing.
                 * Start at 1 as we expect position 0 to be unused. */
                for( ux = 1; ux < intqNUM_VALUES_TO_LOG; ux++ )
                {
                    if( ucNormallyEmptyReceivedValues[ ux ] == 0 )
                    {
                        /* A value is missing. */
                        prvQueueAccessLogError( __LINE__ );
                    }
                    else
                    {
                        if( ucNormallyEmptyReceivedValues[ ux ] == intqHIGH_PRIORITY_TASK1 )
                        {
                            /* Value was placed into the array by task 1. */
                            uxTask1++;
                        }
                        else if( ucNormallyEmptyReceivedValues[ ux ] == intqHIGH_PRIORITY_TASK2 )
                        {
                            /* Value was placed into the array by task 2. */
                            uxTask2++;
                        }
                        else if( ucNormallyEmptyReceivedValues[ ux ] == intqSECOND_INTERRUPT )
                        {
                            uxInterrupts++;
                        }
                    }
                }

                if( uxTask1 < intqMIN_ACCEPTABLE_TASK_COUNT )
                {
                    /* Only task 2 seemed to log any values. */
                    uxErrorCount1++;

                    if( uxErrorCount1 > 2 )
                    {
                        prvQueueAccessLogError( __LINE__ );
                    }
                }
                else
                {
                    uxErrorCount1 = 0;
                }

                if( uxTask2 < intqMIN_ACCEPTABLE_TASK_COUNT )
                {
                    /* Only task 1 seemed to log any values. */
                    uxErrorCount2++;

                    if( uxErrorCount2 > 2 )
                    {
                        prvQueueAccessLogError( __LINE__ );
                    }
                }
                else
                {
                    uxErrorCount2 = 0;
                }

                if( uxInterrupts == 0 )
                {
                    prvQueueAccessLogError( __LINE__ );
                }

                /* Clear the array again, ready to start a new cycle. */
                memset( ucNormallyEmptyReceivedValues, 0x00, sizeof( ucNormallyEmptyReceivedValues ) );

                uxHighPriorityLoops1[ 0 ]++;
                uxValueForNormallyEmptyQueue[ 0 ] = 0;

                /* Suspend ourselves, allowing the lower priority task to
                 * actually receive something from the queue.  Until now it
                 * will have been prevented from doing so by the higher
                 * priority tasks.  The lower priority task will resume us
                 * if it receives something.  We will then resume the other
                 * higher priority task. */
                vTaskSuspend( NULL );
                vTaskResume( xLocalTaskHandles[ SECOND_HIGH_PRIO_EMPTY_TASK_IDX ] );
            }
        }
    }
}
/*-----------------------------------------------------------*/

static void prvLowerPriorityNormallyEmptyTask( void * pvParameters )
{
    UBaseType_t uxValue, uxRxed;

    /* The parameters are not being used so avoid compiler warnings. */
    ( void ) pvParameters;

    for( ; ; )
    {
        if( xQueueReceive( xNormallyEmptyQueue[ 0 ], &uxRxed, intqONE_TICK_DELAY ) != errQUEUE_EMPTY )
        {
            /* A value should only be obtained when the high priority task is
             * suspended. */
            if( eTaskGetState( xLocalTaskHandles[ FIRST_HIGH_PRIO_EMPTY_TASK_IDX ] ) != eSuspended )
            {
                prvQueueAccessLogError( __LINE__ );
            }

            prvRecordValue_NormallyEmpty( uxRxed, intqLOW_PRIORITY_TASK );

            /* Wake the higher priority task again. */
            vTaskResume( xLocalTaskHandles[ FIRST_HIGH_PRIO_EMPTY_TASK_IDX ] );
            uxLowPriorityLoops1[ 0 ]++;
        }
        else
        {
            /* Raise our priority while we send so we can preempt the higher
             *  priority task, and ensure we get the Tx value into the queue. */
            vTaskPrioritySet( NULL, intqHIGHER_PRIORITY + 1 );

            portENTER_CRITICAL();
            {
                uxValueForNormallyEmptyQueue[ 0 ]++;
                uxValue = uxValueForNormallyEmptyQueue[ 0 ];
            }
            portEXIT_CRITICAL();

            if( xQueueSend( xNormallyEmptyQueue[ 0 ], &uxValue, portMAX_DELAY ) != pdPASS )
            {
                prvQueueAccessLogError( __LINE__ );
            }

            vTaskPrioritySet( NULL, intqLOWER_PRIORITY );
        }
    }
}
/*-----------------------------------------------------------*/

static void prv1stHigherPriorityNormallyFullTask( void * pvParameters )
{
    UBaseType_t uxValueToTx, ux, uxInterrupts;

    /* The parameters are not being used so avoid compiler warnings. */
    ( void ) pvParameters;

    /* Make sure the queue starts full or near full.  >> 1 as there are two
     * high priority tasks. */
    for( ux = 0; ux < ( intqQUEUE_LENGTH >> 1 ); ux++ )
    {
        portENTER_CRITICAL();
        {
            uxValueForNormallyFullQueue[ 0 ]++;
            uxValueToTx = uxValueForNormallyFullQueue[ 0 ];
        }
        portEXIT_CRITICAL();

        xQueueSend( xNormallyFullQueue[ 0 ], &uxValueToTx, intqSHORT_DELAY );
    }

    for( ; ; )
    {
        portENTER_CRITICAL();
        {
            uxValueForNormallyFullQueue[ 0 ]++;
            uxValueToTx = uxValueForNormallyFullQueue[ 0 ];
        }
        portEXIT_CRITICAL();

        if( xQueueSend( xNormallyFullQueue[ 0 ], &uxValueToTx, intqSHORT_DELAY ) != pdPASS )
        {
            /* intqHIGH_PRIORITY_TASK2 is never suspended so we would not
             * expect it to ever time out. */
            prvQueueAccessLogError( __LINE__ );
        }

        /* Allow the other task running this code to run. */
        taskYIELD();

        /* Have all the expected values been sent to the queue? */
        if( uxValueToTx > ( intqNUM_VALUES_TO_LOG + intqVALUE_OVERRUN ) )
        {
            /* Make sure the other high priority task completes its send of
             * any values below intqNUM_VALUE_TO_LOG. */
            vTaskDelay( intqSHORT_DELAY );

            vTaskSuspend( xLocalTaskHandles[ SECOND_HIGH_PRIO_FULL_TASK_IDX ] );

            if( xWasSuspended[ 0 ] == pdTRUE )
            {
                /* We would have expected the other high priority task to have
                 * set this back to false by now. */
                prvQueueAccessLogError( __LINE__ );
            }

            /* Set the suspended flag so an error is not logged if the other
             * task recognises a time out when it is unsuspended. */
            xWasSuspended[ 0 ] = pdTRUE;

            /* Check interrupts are also sending. */
            uxInterrupts = 0U;

            /* Start at 1 as we expect position 0 to be unused. */
            for( ux = 1; ux < intqNUM_VALUES_TO_LOG; ux++ )
            {
                if( ucNormallyFullReceivedValues[ ux ] == 0 )
                {
                    /* A value was missing. */
                    prvQueueAccessLogError( __LINE__ );
                }
                else if( ucNormallyFullReceivedValues[ ux ] == intqSECOND_INTERRUPT )
                {
                    uxInterrupts++;
                }
            }

            if( uxInterrupts == 0 )
            {
                /* No writes from interrupts were found.  Are interrupts
                 * actually running? */
                prvQueueAccessLogError( __LINE__ );
            }

            /* Reset the array ready for the next cycle. */
            memset( ucNormallyFullReceivedValues, 0x00, sizeof( ucNormallyFullReceivedValues ) );

            uxHighPriorityLoops2[ 0 ]++;
            uxValueForNormallyFullQueue[ 0 ] = 0;

            /* Suspend ourselves, allowing the lower priority task to
             * actually receive something from the queue.  Until now it
             * will have been prevented from doing so by the higher
             * priority tasks.  The lower priority task will resume us
             * if it receives something.  We will then resume the other
             * higher priority task. */
            vTaskSuspend( NULL );
            vTaskResume( xLocalTaskHandles[ SECOND_HIGH_PRIO_FULL_TASK_IDX ] );
        }
    }
}
/*-----------------------------------------------------------*/

static void prv2ndHigherPriorityNormallyFullTask( void * pvParameters )
{
    UBaseType_t uxValueToTx, ux;

    /* The parameters are not being used so avoid compiler warnings. */
    ( void ) pvParameters;

    /* Make sure the queue starts full or near full.  >> 1 as there are two
     * high priority tasks. */
    for( ux = 0; ux < ( intqQUEUE_LENGTH >> 1 ); ux++ )
    {
        portENTER_CRITICAL();
        {
            uxValueForNormallyFullQueue[ 0 ]++;
            uxValueToTx = uxValueForNormallyFullQueue[ 0 ];
        }
        portEXIT_CRITICAL();

        xQueueSend( xNormallyFullQueue[ 0 ], &uxValueToTx, intqSHORT_DELAY );
    }

    for( ; ; )
    {
        portENTER_CRITICAL();
        {
            uxValueForNormallyFullQueue[ 0 ]++;
            uxValueToTx = uxValueForNormallyFullQueue[ 0 ];
        }
        portEXIT_CRITICAL();

        if( xQueueSend( xNormallyFullQueue[ 0 ], &uxValueToTx, intqSHORT_DELAY ) != pdPASS )
        {
            if( xWasSuspended[ 0 ] != pdTRUE )
            {
                /* It is ok to time out if the task has been suspended. */
                prvQueueAccessLogError( __LINE__ );
            }
        }

        xWasSuspended[ 0 ] = pdFALSE;

        taskYIELD();
    }
}
/*-----------------------------------------------------------*/

static void prvLowerPriorityNormallyFullTask( void * pvParameters )
{
    UBaseType_t uxValue, uxTxed = 9999;

    /* The parameters are not being used so avoid compiler warnings. */
    ( void ) pvParameters;

    for( ; ; )
    {
        if( xQueueSend( xNormallyFullQueue[ 0 ], &uxTxed, intqONE_TICK_DELAY ) != errQUEUE_FULL )
        {
            /* Should only succeed when the higher priority task is suspended */
            if( eTaskGetState( xLocalTaskHandles[ FIRST_HIGH_PRIO_FULL_TASK_IDX ] ) != eSuspended )
            {
                prvQueueAccessLogError( __LINE__ );
            }

            vTaskResume( xLocalTaskHandles[ FIRST_HIGH_PRIO_FULL_TASK_IDX ] );
            uxLowPriorityLoops2[ 0 ]++;
        }
        else
        {
            /* Raise our priority while we receive so we can preempt the higher
             * priority task, and ensure we get the value from the queue. */
            vTaskPrioritySet( NULL, intqHIGHER_PRIORITY + 1 );

            if( xQueueReceive( xNormallyFullQueue[ 0 ], &uxValue, portMAX_DELAY ) != pdPASS )
            {
                prvQueueAccessLogError( __LINE__ );
            }
            else
            {
                prvRecordValue_NormallyFull( uxValue, intqLOW_PRIORITY_TASK );
            }

            vTaskPrioritySet( NULL, intqLOWER_PRIORITY );
        }
    }
}
/*-----------------------------------------------------------*/

BaseType_t xFirstTimerHandler( void )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UBaseType_t uxRxedValue;
    static UBaseType_t uxNextOperation = 0;

    /* Called from a timer interrupt.  Perform various read and write
     * accesses on the queues. */

    uxNextOperation++;

    if( uxNextOperation & ( UBaseType_t ) 0x01 )
    {
        timerNORMALLY_EMPTY_TX();
        timerNORMALLY_EMPTY_TX();
        timerNORMALLY_EMPTY_TX();
    }
    else
    {
        timerNORMALLY_FULL_RX();
        timerNORMALLY_FULL_RX();
        timerNORMALLY_FULL_RX();
    }

    return xHigherPriorityTaskWoken;
}
/*-----------------------------------------------------------*/

BaseType_t xSecondTimerHandler( void )
{
    UBaseType_t uxRxedValue;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static UBaseType_t uxNextOperation = 0;

    /* Called from a timer interrupt.  Perform various read and write
     * accesses on the queues. */

    uxNextOperation++;

    if( uxNextOperation & ( UBaseType_t ) 0x01 )
    {
        timerNORMALLY_EMPTY_TX();
        timerNORMALLY_EMPTY_TX();

        timerNORMALLY_EMPTY_RX();
        timerNORMALLY_EMPTY_RX();
    }
    else
    {
        timerNORMALLY_FULL_RX();
        timerNORMALLY_FULL_TX();
        timerNORMALLY_FULL_TX();
        timerNORMALLY_FULL_TX();
    }

    return xHigherPriorityTaskWoken;
}
/*-----------------------------------------------------------*/


BaseType_t xAreIntQueueTasksStillRunning( void )
{
    static UBaseType_t uxLastHighPriorityLoops1 = 0, uxLastHighPriorityLoops2 = 0, uxLastLowPriorityLoops1 = 0, uxLastLowPriorityLoops2 = 0;

    /* xErrorStatus can be set outside of this function.  This function just
     * checks that all the tasks are still cycling. */

    if( uxHighPriorityLoops1[ 0 ] == uxLastHighPriorityLoops1 )
    {
        /* The high priority 1 task has stalled. */
        prvQueueAccessLogError( __LINE__ );
    }

    uxLastHighPriorityLoops1 = uxHighPriorityLoops1[ 0 ];

    if( uxHighPriorityLoops2[ 0 ] == uxLastHighPriorityLoops2 )
    {
        /* The high priority 2 task has stalled. */
        prvQueueAccessLogError( __LINE__ );
    }

    uxLastHighPriorityLoops2 = uxHighPriorityLoops2[ 0 ];

    if( uxLowPriorityLoops1[ 0 ] == uxLastLowPriorityLoops1 )
    {
        /* The low priority 1 task has stalled. */
        prvQueueAccessLogError( __LINE__ );
    }

    uxLastLowPriorityLoops1 = uxLowPriorityLoops1[ 0 ];

    if( uxLowPriorityLoops2[ 0 ] == uxLastLowPriorityLoops2 )
    {
        /* The low priority 2 task has stalled. */
        prvQueueAccessLogError( __LINE__ );
    }

    uxLastLowPriorityLoops2 = uxLowPriorityLoops2[ 0 ];

    return xErrorStatus[ 0 ];
}
/*-----------------------------------------------------------*/

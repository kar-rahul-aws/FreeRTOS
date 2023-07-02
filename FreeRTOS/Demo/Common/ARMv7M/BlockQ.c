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
 * Creates six tasks that operate on three queues as follows:
 *
 * The first two tasks send and receive an incrementing number to/from a queue.
 * One task acts as a producer and the other as the consumer.  The consumer is a
 * higher priority than the producer and is set to block on queue reads.  The queue
 * only has space for one item - as soon as the producer posts a message on the
 * queue the consumer will unblock, pre-empt the producer, and remove the item.
 *
 * The second two tasks work the other way around.  Again the queue used only has
 * enough space for one item.  This time the consumer has a lower priority than the
 * producer.  The producer will try to post on the queue blocking when the queue is
 * full.  When the consumer wakes it will remove the item from the queue, causing
 * the producer to unblock, pre-empt the consumer, and immediately re-fill the
 * queue.
 *
 * The last two tasks use the same queue producer and consumer functions.  This time the queue has
 * enough space for lots of items and the tasks operate at the same priority.  The
 * producer will execute, placing items into the queue.  The consumer will start
 * executing when either the queue becomes full (causing the producer to block) or
 * a context switch occurs (tasks of the same priority will time slice).
 *
 */

#include <stdlib.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Demo program include files. */
#include "BlockQ.h"

#define blckqSTACK_SIZE       configMINIMAL_STACK_SIZE
#define blckqNUM_TASK_SETS    ( 3 )

#define blckqSHARED_MEM_SIZE_HALF_WORDS    ( 16 )
#define blckqSHARED_MEM_SIZE_BYTES         ( 32 )

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 0 )
    #error This example cannot be used if dynamic allocation is not allowed.
#endif

/* Structure used to pass parameters to the blocking queue tasks. */
typedef struct BLOCKING_QUEUE_PARAMETERS
{
    QueueHandle_t xQueue;             /*< The queue to be used by the task. */
    TickType_t xBlockTime;            /*< The block time to use on queue reads/writes. */
    volatile short * psCheckVariable; /*< Incremented on each successful cycle to check the task is still running. */
    uint32_t unused[ 5 ];
} xBlockingQueueParameters;

/* Task function that creates an incrementing number and posts it on a queue. */
static portTASK_FUNCTION_PROTO( vBlockingQueueProducer, pvParameters );

/* Task function that removes the incrementing number from a queue and checks that
 * it is the expected number. */
static portTASK_FUNCTION_PROTO( vBlockingQueueConsumer, pvParameters );

/* Variables which are incremented each time an item is removed from a queue, and
 * found to be the expected value.
 * These are used to check that the tasks are still running. */
static volatile short sBlockingConsumerCount[ blckqSHARED_MEM_SIZE_HALF_WORDS ] __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* Variable which are incremented each time an item is posted on a queue.   These
 * are used to check that the tasks are still running. */
static volatile short sBlockingProducerCount[ blckqSHARED_MEM_SIZE_HALF_WORDS ] __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

static xBlockingQueueParameters xQueueParameters1 __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) );
static xBlockingQueueParameters xQueueParameters2 __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) );
static xBlockingQueueParameters xQueueParameters3 __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) );
static xBlockingQueueParameters xQueueParameters4 __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) );
static xBlockingQueueParameters xQueueParameters5 __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) );
static xBlockingQueueParameters xQueueParameters6 __attribute__( ( aligned( blckqSHARED_MEM_SIZE_BYTES ) ) );
/*-----------------------------------------------------------*/

void vStartBlockingQueueTasks( UBaseType_t uxPriority )
{
    xBlockingQueueParameters * pxQueueParameters1, * pxQueueParameters2;
    xBlockingQueueParameters * pxQueueParameters3, * pxQueueParameters4;
    xBlockingQueueParameters * pxQueueParameters5, * pxQueueParameters6;
    const UBaseType_t uxQueueSize1 = 1, uxQueueSize5 = 5;
    const TickType_t xBlockTime = pdMS_TO_TICKS( ( TickType_t ) 1000 );
    const TickType_t xDontBlock = ( TickType_t ) 0;

    static StackType_t xBlockingQueueConsumerB1TaskStack[ blckqSTACK_SIZE ] __attribute__( ( aligned( blckqSTACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xBlockingQueueProducerB2TaskStack[ blckqSTACK_SIZE ] __attribute__( ( aligned( blckqSTACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xBlockingQueueConsumerB3TaskStack[ blckqSTACK_SIZE ] __attribute__( ( aligned( blckqSTACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xBlockingQueueProducerB4TaskStack[ blckqSTACK_SIZE ] __attribute__( ( aligned( blckqSTACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xBlockingQueueProducerB5TaskStack[ blckqSTACK_SIZE ] __attribute__( ( aligned( blckqSTACK_SIZE * sizeof( StackType_t ) ) ) );
    static StackType_t xBlockingQueueConsumerB6TaskStack[ blckqSTACK_SIZE ] __attribute__( ( aligned( blckqSTACK_SIZE * sizeof( StackType_t ) ) ) );

    /* Create the first two tasks as described at the top of the file. */

    /* First create the structure used to pass parameters to the consumer tasks. */
    pxQueueParameters1 = &( xQueueParameters1 );

    /* Create the queue used by the first two tasks to pass the incrementing number.
     * Pass a pointer to the queue in the parameter structure. */
    pxQueueParameters1->xQueue = xQueueCreate( uxQueueSize1, ( UBaseType_t ) sizeof( uint16_t ) );

    /* The consumer is created first so gets a block time as described above. */
    pxQueueParameters1->xBlockTime = xBlockTime;

    /* Pass in the variable that this task is going to increment so we can check it
     * is still running. */
    pxQueueParameters1->psCheckVariable = &( sBlockingConsumerCount[ 0 ] );

    /* Create the structure used to pass parameters to the producer task. */
    pxQueueParameters2 = &( xQueueParameters2 );

    /* Pass the queue to this task also, using the parameter structure. */
    pxQueueParameters2->xQueue = pxQueueParameters1->xQueue;

    /* The producer is not going to block - as soon as it posts the consumer will
     * wake and remove the item so the producer should always have room to post. */
    pxQueueParameters2->xBlockTime = xDontBlock;

    /* Pass in the variable that this task is going to increment so we can check
     * it is still running. */
    pxQueueParameters2->psCheckVariable = &( sBlockingProducerCount[ 0 ] );


    /* Note the producer has a lower priority than the consumer when the tasks are
     * spawned. */


    TaskParameters_t xBlockingQueueConsumerB1Task =
        {
            .pvTaskCode      = vBlockingQueueConsumer,
            .pcName          = "QConsB1",
            .usStackDepth    = blckqSTACK_SIZE,
            .pvParameters    = ( void * ) pxQueueParameters1,
            .uxPriority      = uxPriority,
            .puxStackBuffer  = xBlockingQueueConsumerB1TaskStack,
            .xRegions        =    {
                                    { ( void * ) &( sBlockingConsumerCount[ 0 ] ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { &( xQueueParameters1 ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    #if ( configTOTAL_MPU_REGIONS == 16 )
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    #endif
                                    { 0,                0,                    0                                                        }
                                }
        };
    TaskParameters_t xBlockingQueueProducerB2Task =
        {
            .pvTaskCode      = vBlockingQueueProducer,
            .pcName          = "QProdB2",
            .usStackDepth    = blckqSTACK_SIZE,
            .pvParameters    = ( void * ) pxQueueParameters2,
            .uxPriority      = tskIDLE_PRIORITY,
            .puxStackBuffer  = xBlockingQueueProducerB2TaskStack,
            .xRegions        =    {
                                    { ( void * ) &( sBlockingProducerCount[ 0 ] ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { &( xQueueParameters2 ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    #if ( configTOTAL_MPU_REGIONS == 16 )
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    #endif
                                    { 0,                0,                    0                                                        }
                                }
        };

    xTaskCreateRestricted( &( xBlockingQueueConsumerB1Task ), NULL );
    xTaskCreateRestricted( &( xBlockingQueueProducerB2Task ), NULL );


    /* Create the second two tasks as described at the top of the file.   This uses
     * the same mechanism but reverses the task priorities. */

    pxQueueParameters3 = &( xQueueParameters3 );
    pxQueueParameters3->xQueue = xQueueCreate( uxQueueSize1, ( UBaseType_t ) sizeof( uint16_t ) );
    pxQueueParameters3->xBlockTime = xDontBlock;
    pxQueueParameters3->psCheckVariable = &( sBlockingConsumerCount[ 1 ] );

    pxQueueParameters4 = &( xQueueParameters4 );
    pxQueueParameters4->xQueue = pxQueueParameters3->xQueue;
    pxQueueParameters4->xBlockTime = xBlockTime;
    pxQueueParameters4->psCheckVariable = &( sBlockingProducerCount[ 1 ] );

    TaskParameters_t xBlockingQueueConsumerB3Task =
        {
            .pvTaskCode      = vBlockingQueueConsumer,
            .pcName          = "QConsB3",
            .usStackDepth    = blckqSTACK_SIZE,
            .pvParameters    = ( void * ) pxQueueParameters3,
            .uxPriority      = tskIDLE_PRIORITY,
            .puxStackBuffer  = xBlockingQueueConsumerB3TaskStack,
            .xRegions        =    {
                                    { ( void * ) &( sBlockingConsumerCount[ 0 ] ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                             ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { &( xQueueParameters3 ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    #if ( configTOTAL_MPU_REGIONS == 16 )
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    #endif
                                    { 0,                0,                    0                                                        }
                                }
        };
    TaskParameters_t xBlockingQueueProducerB4Task =
        {
            .pvTaskCode      = vBlockingQueueProducer,
            .pcName          = "QProdB4",
            .usStackDepth    = blckqSTACK_SIZE,
            .pvParameters    = ( void * ) pxQueueParameters4,
            .uxPriority      = uxPriority,
            .puxStackBuffer  = xBlockingQueueProducerB4TaskStack,
            .xRegions        =    {
                                    { ( void * ) &( sBlockingProducerCount[ 0 ] ), blckqSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { &( xQueueParameters4 ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    #if ( configTOTAL_MPU_REGIONS == 16 )
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    #endif
                                    { 0,                0,                    0                                                        }
                                }
        };

    xTaskCreateRestricted( &( xBlockingQueueConsumerB3Task ), NULL );
    xTaskCreateRestricted( &( xBlockingQueueProducerB4Task ), NULL );

    /* Create the last two tasks as described above.  The mechanism is again just
     * the same.  This time both parameter structures are given a block time. */
    pxQueueParameters5 = &( xQueueParameters5 );
    pxQueueParameters5->xQueue = xQueueCreate( uxQueueSize5, ( UBaseType_t ) sizeof( uint16_t ) );
    pxQueueParameters5->xBlockTime = xBlockTime;
    pxQueueParameters5->psCheckVariable = &( sBlockingProducerCount[ 2 ] );

    pxQueueParameters6 = &( xQueueParameters6 );
    pxQueueParameters6->xQueue = pxQueueParameters5->xQueue;
    pxQueueParameters6->xBlockTime = xBlockTime;
    pxQueueParameters6->psCheckVariable = &( sBlockingConsumerCount[ 2 ] );

    TaskParameters_t xBlockingQueueProducerB5Task =
        {
            .pvTaskCode      = vBlockingQueueProducer,
            .pcName          = "QProdB5",
            .usStackDepth    = blckqSTACK_SIZE,
            .pvParameters    = ( void * ) pxQueueParameters5,
            .uxPriority      = tskIDLE_PRIORITY,
            .puxStackBuffer  = xBlockingQueueProducerB5TaskStack,
            .xRegions        =    {
                                    { ( void * ) &( sBlockingProducerCount[ 0 ] ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { &( xQueueParameters5 ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    #if ( configTOTAL_MPU_REGIONS == 16 )
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    #endif
                                    { 0,                0,                    0                                                        }
                                }
        };
    TaskParameters_t xBlockingQueueConsumerB6Task =
        {
            .pvTaskCode      = vBlockingQueueConsumer,
            .pcName          = "QConsB6",
            .usStackDepth    = blckqSTACK_SIZE,
            .pvParameters    = ( void * ) pxQueueParameters6,
            .uxPriority      = tskIDLE_PRIORITY,
            .puxStackBuffer  = xBlockingQueueConsumerB6TaskStack,
            .xRegions        =    {
                                    { ( void * ) &( sBlockingConsumerCount[ 0 ] ), blckqSHARED_MEM_SIZE_BYTES,
                                        ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    { &( xQueueParameters6 ), blckqSHARED_MEM_SIZE_BYTES,
                                      ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
                                        ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
                                    },
                                    #if ( configTOTAL_MPU_REGIONS == 16 )
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    { 0,                0,                    0                                                        },
                                    #endif
                                    { 0,                0,                    0                                                        }
                                }
        };

    xTaskCreateRestricted( &( xBlockingQueueProducerB5Task ), NULL );
    xTaskCreateRestricted( &( xBlockingQueueConsumerB6Task ), NULL );
}
/*-----------------------------------------------------------*/

static portTASK_FUNCTION( vBlockingQueueProducer, pvParameters )
{
    uint16_t usValue = 0;
    xBlockingQueueParameters * pxQueueParameters;
    short sErrorEverOccurred = pdFALSE;

    pxQueueParameters = ( xBlockingQueueParameters * ) pvParameters;

    for( ; ; )
    {
        if( xQueueSend( pxQueueParameters->xQueue, ( void * ) &usValue, pxQueueParameters->xBlockTime ) != pdPASS )
        {
            sErrorEverOccurred = pdTRUE;
        }
        else
        {
            /* We have successfully posted a message, so increment the variable
             * used to check we are still running. */
            if( sErrorEverOccurred == pdFALSE )
            {
                ( *pxQueueParameters->psCheckVariable )++;
            }

            /* Increment the variable we are going to post next time round.  The
             * consumer will expect the numbers to follow in numerical order. */
            ++usValue;

            #if configUSE_PREEMPTION == 0
                taskYIELD();
            #endif
        }
    }
}
/*-----------------------------------------------------------*/

static portTASK_FUNCTION( vBlockingQueueConsumer, pvParameters )
{
    uint16_t usData, usExpectedValue = 0;
    xBlockingQueueParameters * pxQueueParameters;
    short sErrorEverOccurred = pdFALSE;

    pxQueueParameters = ( xBlockingQueueParameters * ) pvParameters;

    for( ; ; )
    {
        if( xQueueReceive( pxQueueParameters->xQueue, &usData, pxQueueParameters->xBlockTime ) == pdPASS )
        {
            if( usData != usExpectedValue )
            {
                /* Catch-up. */
                usExpectedValue = usData;

                sErrorEverOccurred = pdTRUE;
            }
            else
            {
                /* We have successfully received a message, so increment the
                 * variable used to check we are still running. */
                if( sErrorEverOccurred == pdFALSE )
                {
                    ( *pxQueueParameters->psCheckVariable )++;
                }

                /* Increment the value we expect to remove from the queue next time
                 * round. */
                ++usExpectedValue;
            }

            #if configUSE_PREEMPTION == 0
            {
                if( pxQueueParameters->xBlockTime == 0 )
                {
                    taskYIELD();
                }
            }
            #endif
        }
    }
}
/*-----------------------------------------------------------*/

/* This is called to check that all the created tasks are still running. */
BaseType_t xAreBlockingQueuesStillRunning( void )
{
    static short sLastBlockingConsumerCount[ blckqNUM_TASK_SETS ] = { ( uint16_t ) 0, ( uint16_t ) 0, ( uint16_t ) 0 };
    static short sLastBlockingProducerCount[ blckqNUM_TASK_SETS ] = { ( uint16_t ) 0, ( uint16_t ) 0, ( uint16_t ) 0 };
    BaseType_t xReturn = pdPASS, xTasks;

    /* Not too worried about mutual exclusion on these variables as they are 16
     * bits and we are only reading them. We also only care to see if they have
     * changed or not.
     *
     * Loop through each check variable to and return pdFALSE if any are found not
     * to have changed since the last call. */

    for( xTasks = 0; xTasks < blckqNUM_TASK_SETS; xTasks++ )
    {
        if( sBlockingConsumerCount[ xTasks ] == sLastBlockingConsumerCount[ xTasks ] )
        {
            xReturn = pdFALSE;
        }

        sLastBlockingConsumerCount[ xTasks ] = sBlockingConsumerCount[ xTasks ];

        if( sBlockingProducerCount[ xTasks ] == sLastBlockingProducerCount[ xTasks ] )
        {
            xReturn = pdFALSE;
        }

        sLastBlockingProducerCount[ xTasks ] = sBlockingProducerCount[ xTasks ];
    }

    return xReturn;
}

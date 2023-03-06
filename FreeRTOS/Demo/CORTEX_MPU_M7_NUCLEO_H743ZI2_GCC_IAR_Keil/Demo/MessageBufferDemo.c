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

/* Standard includes. */
#include "stdio.h"
#include "string.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

/* Demo app includes. */
#include "MessageBufferDemo.h"

#define mbSHARED_MEM_SIZE_CHAR			  	( 4 )
#define mbSHARED_MEM_SIZE_WORDS             ( 8 )
#define mbSHARED_MEM_SIZE_HALF_WORDS		( 16 )
#define mbSHARED_MEM_SIZE_BYTES             ( 32 )

/* The number of bytes of storage in the message buffers used in this test. */
#define mbMESSAGE_BUFFER_LENGTH_BYTES      ( ( size_t ) 50 )

/* The number of additional bytes used to store the length of each message. */
#define mbBYTES_TO_STORE_MESSAGE_LENGTH    ( sizeof( configMESSAGE_BUFFER_LENGTH_TYPE ) )

/* Start and end ASCII characters used in messages sent to the buffers. */
#define mbASCII_SPACE                      32
#define mbASCII_TILDA                      126

/* Defines the number of tasks to create in this test and demo. */
#define mbNUMBER_OF_ECHO_CLIENTS           ( 2 )
#define mbNUMBER_OF_SENDER_TASKS           ( 2 )

/* Priority of the test tasks.  The send and receive go from low to high
 * priority tasks, and from high to low priority tasks. */
#define mbLOWER_PRIORITY                   ( tskIDLE_PRIORITY )
#define mbHIGHER_PRIORITY                  ( tskIDLE_PRIORITY + 1 )
#define mbHIGHEST_PRIORITY				   ( tskIDLE_PRIORITY + 2 )

/* Block times used when sending and receiving from the message buffers. */
#define mbRX_TX_BLOCK_TIME                 pdMS_TO_TICKS( 175UL )

/* A block time of 0 means "don't block". */
#define mbDONT_BLOCK                       ( 0 )

/*-----------------------------------------------------------*/

/*
 * Performs various tests that do not require multiple tasks to interact.
 */
static void prvSingleTaskTests( MessageBufferHandle_t xMessageBuffer );

/*
 * Tests sending and receiving various lengths of messages via a message buffer.
 * The echo client sends the messages to the echo server, which then sends the
 * message back to the echo client which, checks it receives exactly what it
 * sent.
 */
static void prvEchoClient( void * pvParameters );
static void prvEchoServer( void * pvParameters );

/*
 * Tasks that send and receive to a message buffer at a low priority and without
 * blocking, so the send and receive functions interleave in time as the tasks
 * are switched in and out.
 */
static void prvNonBlockingReceiverTask( void * pvParameters );
static void prvNonBlockingSenderTask( void * pvParameters );

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

/* This file tests both statically and dynamically allocated message buffers.
 * Allocate the structures and buffers to be used by the statically allocated
 * objects, which get used in the echo tests. */
    static void prvReceiverTask( void * pvParameters );
    static void prvSenderTask( void * pvParameters );

    static TaskHandle_t xReceiverTaskHandles[ mbSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

    static MessageBufferHandle_t xReceiverTaskMsgBuffers[ mbSHARED_MEM_SIZE_WORDS ] __attribute__((aligned(mbSHARED_MEM_SIZE_BYTES))) = { NULL };

    static StaticMessageBuffer_t xStaticMessageBuffers[ mbNUMBER_OF_ECHO_CLIENTS ];
    static uint8_t ucBufferStorage[ mbNUMBER_OF_SENDER_TASKS ][ mbMESSAGE_BUFFER_LENGTH_BYTES + 1 ];
    static uint32_t ulSenderLoopCounters[ mbSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };
#endif /* configSUPPORT_STATIC_ALLOCATION */


#if ( configRUN_ADDITIONAL_TESTS == 1 )
    #define mbCOHERENCE_TEST_BUFFER_SIZE                  20
    #define mbCOHERENCE_TEST_BYTES_WRITTEN                5
    #define mbBYTES_TO_STORE_MESSAGE_LENGTH               ( sizeof( configMESSAGE_BUFFER_LENGTH_TYPE ) )
    #define mbEXPECTED_FREE_BYTES_AFTER_WRITING_STRING    ( mbCOHERENCE_TEST_BUFFER_SIZE - ( mbCOHERENCE_TEST_BYTES_WRITTEN + mbBYTES_TO_STORE_MESSAGE_LENGTH ) )

    static void prvSpaceAvailableCoherenceActor( void * pvParameters );
    static void prvSpaceAvailableCoherenceTester( void * pvParameters );
    static MessageBufferHandle_t xCoherenceTestMessageBuffer = NULL;

    static uint32_t ulSizeCoherencyTestCycles = 0UL;
#endif /* if ( configRUN_ADDITIONAL_TESTS == 1 ) */

/*-----------------------------------------------------------*/

/* The buffers used by the echo client and server tasks. */
typedef struct ECHO_MESSAGE_BUFFERS
{
    /* Handles to the data structures that describe the message buffers. */
    MessageBufferHandle_t xEchoClientBuffer;
    MessageBufferHandle_t xEchoServerBuffer;
} EchoMessageBuffers_t;
static uint32_t ulEchoLoopCounters[ mbSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* The non-blocking tasks monitor their operation, and if no errors have been
 * found, increment ulNonBlockingRxCounter.  xAreMessageBufferTasksStillRunning()
 * then checks ulNonBlockingRxCounter and only returns pdPASS if
 * ulNonBlockingRxCounter is still incrementing. */
static uint32_t ulNonBlockingRxCounter[ mbSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };

/* A message that is longer than the buffer, parts of which are written to the
 * message buffer to test writing different lengths at different offsets. */
static const char * pc55ByteString[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { "One two three four five six seven eight nine ten eleven" };

static EchoMessageBuffers_t xMessageBuffers[ mbSHARED_MEM_SIZE_CHAR ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

static TaskHandle_t xEchoClientTaskHandle[ mbSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

/* Remember the required stack size so tasks can be created at run time (after
 * initialization time. */
static configSTACK_DEPTH_TYPE xBlockingStackSize = 0;

/* Used to define strings , since pvPortMalloc cannot be used */
static char pcReceivedString1[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );
static char pcReceivedString2[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );
static char pcStringToSend1[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );
static char pcStringReceived1[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );
static char pcStringToSend2[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );
static char pcStringReceived2[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );
static uint8_t pucFullBuffer[ mbSHARED_MEM_SIZE_BYTES * 2 ] __attribute__( ( aligned( mbSHARED_MEM_SIZE_BYTES * 2 ) ) );


/*-----------------------------------------------------------*/

void vStartMessageBufferTasks( configSTACK_DEPTH_TYPE xStackSize )
{
    MessageBufferHandle_t xMessageBuffer;

    #ifndef configMESSAGE_BUFFER_BLOCK_TASK_STACK_SIZE
        xBlockingStackSize = ( xStackSize + ( xStackSize >> 1U ) );
    #else
        xBlockingStackSize = configMESSAGE_BUFFER_BLOCK_TASK_STACK_SIZE;
    #endif

        /*Stack Definitions*/
        static StackType_t xEchoServerTask1Stack[ configMINIMAL_STACK_SIZE * 2 ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * 2 * sizeof( StackType_t ) ) ) );
        static StackType_t xEchoServerTask2Stack[ configMINIMAL_STACK_SIZE * 2 ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * 2 * sizeof( StackType_t ) ) ) );
        static StackType_t xNonBlockingReceiverTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
        static StackType_t xNonBlockingSenderTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
        static StackType_t xEchoClientTask1Stack[ configMINIMAL_STACK_SIZE * 2	] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * 2 * sizeof( StackType_t ) ) ) );
        static StackType_t xEchoClientTask2Stack[ configMINIMAL_STACK_SIZE * 2	] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * 2 * sizeof( StackType_t ) ) ) );

        /* Create the message buffer used to send data from the client to the server,
         * and the message buffer used to echo the data from the server back to the
         * client. */
        int idx;
        for( idx = 0; idx < 2; idx++)
        {
            xMessageBuffers[ idx ].xEchoClientBuffer = xMessageBufferCreate( mbMESSAGE_BUFFER_LENGTH_BYTES );
            xMessageBuffers[ idx ].xEchoServerBuffer = xMessageBufferCreate( mbMESSAGE_BUFFER_LENGTH_BYTES );
        }

        /* When the stream buffers have been created the echo client task can be
         * created.  If this server task has the higher priority then the client task
         * is created at the lower priority - if this server task has the lower
         * priority then the client task is created at the higher priority.
         * Currently, keeping the parameters as NULL, will assign then in the server task*/

        TaskParameters_t xEchoClientTask1Parameters =
        {
            .pvTaskCode      = 	prvEchoClient,
            .pcName          = 	"EchoClient",
            .usStackDepth    = 	configMINIMAL_STACK_SIZE,
            .pvParameters    = 	(void *) &xMessageBuffers[ 0 ],
            .uxPriority      =	mbLOWER_PRIORITY ,
            .puxStackBuffer  = 	xEchoClientTask1Stack,
            .xRegions        =    {
									{ ( void * ) &( pcStringToSend1[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( pcStringReceived1[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( ulEchoLoopCounters[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xMessageBuffers[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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

        TaskParameters_t xEchoClientTask2Parameters =
        {
            .pvTaskCode      = 	prvEchoClient,
            .pcName          = 	"EchoClient",
            .usStackDepth    = 	configMINIMAL_STACK_SIZE,
            .pvParameters    = 	(void *) &xMessageBuffers[ 1 ],
            .uxPriority      =	mbLOWER_PRIORITY ,
            .puxStackBuffer  = 	xEchoClientTask2Stack,
            .xRegions        =    {
									{ ( void * ) &( pcStringToSend2[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( pcStringReceived2[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( ulEchoLoopCounters[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xMessageBuffers[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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

        /*Creating client task */
        xTaskCreateRestricted( &( xEchoClientTask1Parameters ), &( xEchoClientTaskHandle[ 0 ] ) );
        xTaskCreateRestricted( &( xEchoClientTask2Parameters ), &( xEchoClientTaskHandle[ 1 ] ) );

    /* The echo servers sets up the message buffers before creating the echo
     * client tasks.  One set of tasks has the server as the higher priority, and
     * the other has the client as the higher priority. */

        TaskParameters_t xEchoServerTask1Parameters =
        {
            .pvTaskCode      = prvEchoServer,
            .pcName          = "1EchoServer",
            .usStackDepth    = xBlockingStackSize,
            .pvParameters    = (void *) &xMessageBuffers[ 0 ],
            .uxPriority      = ( mbHIGHEST_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer  = xEchoServerTask1Stack,
            .xRegions        =    {
									{ ( void * ) &( pucFullBuffer[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( pcReceivedString2[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xMessageBuffers[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xEchoClientTaskHandle[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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

        TaskParameters_t xEchoServerTask2Parameters =
        {
            .pvTaskCode      = prvEchoServer,
            .pcName          = "2StrEchoServer",
            .usStackDepth    = xBlockingStackSize,
            .pvParameters    = (void *) &xMessageBuffers[ 1 ],
            .uxPriority      = mbHIGHER_PRIORITY,
            .puxStackBuffer  = xEchoServerTask2Stack,
            .xRegions        =    {
									{ ( void * ) &( pucFullBuffer[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( pcReceivedString1[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
										( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xMessageBuffers[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xEchoClientTaskHandle[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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

        xTaskCreateRestricted( &( xEchoServerTask1Parameters ), NULL );
        xTaskCreateRestricted( &( xEchoServerTask2Parameters ), NULL );


    /* The non blocking tasks run continuously and will interleave with each
     * other, so must be created at the lowest priority.  The message buffer they
     * use is created and passed in using the task's parameter. */
    xMessageBuffer = xMessageBufferCreate( mbMESSAGE_BUFFER_LENGTH_BYTES );

    TaskParameters_t xNonBlockingReceiverTaskParameters =
    {
        .pvTaskCode      = prvNonBlockingReceiverTask,
        .pcName          = "NonBlkRx",
        .usStackDepth    = xStackSize,
        .pvParameters    = (void *) xMessageBuffer,
        .uxPriority      = mbHIGHER_PRIORITY,
        .puxStackBuffer  = xNonBlockingReceiverTaskStack,
        .xRegions        =    {
								{ ( void * ) &( ulNonBlockingRxCounter[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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

    TaskParameters_t xNonBlockingSenderTaskParameters =
    {
        .pvTaskCode      = prvNonBlockingSenderTask,
        .pcName          = "NonBlkTx",
        .usStackDepth    = xStackSize,
        .pvParameters    = (void *) xMessageBuffer,
        .uxPriority      = mbHIGHER_PRIORITY,
        .puxStackBuffer  = xNonBlockingSenderTaskStack,
        .xRegions        =    {
								{ 0,                0,                    0                                                        },
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

    xTaskCreateRestricted( &( xNonBlockingReceiverTaskParameters ), NULL );
    xTaskCreateRestricted( &( xNonBlockingSenderTaskParameters ), NULL );

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    {

    	int idx;
    	for( idx = 0; idx < 2; idx++)
    	{
    		xReceiverTaskMsgBuffers[ idx ] = xMessageBufferCreateStatic( sizeof( ucBufferStorage ) / mbNUMBER_OF_SENDER_TASKS, /* The number of bytes in each buffer in the array. */
                                                         &( ucBufferStorage[ idx ][ 0 ] ),                 /* The address of the buffer to use within the array. */
                                                         &( xStaticMessageBuffers[ idx ] ) );              /* The static message buffer structure to use within the array. */
    	}

        static StackType_t xReceivingTask1Stack[configMINIMAL_STACK_SIZE * 2] __attribute__((aligned(configMINIMAL_STACK_SIZE * 2 * sizeof(StackType_t))));
        static StackType_t xReceivingTask2Stack[configMINIMAL_STACK_SIZE * 2] __attribute__((aligned(configMINIMAL_STACK_SIZE * 2 * sizeof(StackType_t))));

        TaskParameters_t xReceivingTask1Parameters =
        {
            .pvTaskCode = prvReceiverTask,
            .pcName = "MsgReceiver",
            .usStackDepth = xBlockingStackSize,
            .pvParameters = (void*) xReceiverTaskMsgBuffers[ 0 ],
            .uxPriority = mbLOWER_PRIORITY,
            .puxStackBuffer = xReceivingTask1Stack,
	        .xRegions        =    {
									{ 0,                0,                    0                                                        },
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

        TaskParameters_t xReceivingTask2Parameters =
        {
            .pvTaskCode = prvReceiverTask,
            .pcName = "MsgReceiver",
            .usStackDepth = xBlockingStackSize,
            .pvParameters = (void*) xReceiverTaskMsgBuffers[ 1 ],
            .uxPriority = mbLOWER_PRIORITY,
            .puxStackBuffer = xReceivingTask2Stack,
	        .xRegions        =    {
									{ 0,                0,                    0                                                        },
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

        xTaskCreateRestricted( &( xReceivingTask1Parameters ), &( xReceiverTaskHandles[ 0 ] ) );
        xTaskCreateRestricted( &( xReceivingTask2Parameters ), &( xReceiverTaskHandles[ 1 ] ) );
        /* The sender tasks set up the message buffers before creating the
         * receiver tasks.  Priorities must be 0 and 1 as the priority is used to
         * index into the xStaticMessageBuffers and ucBufferStorage arrays. */

        static StackType_t xSenderTask1Stack[ configMINIMAL_STACK_SIZE * 2 ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * 2 * sizeof( StackType_t ) ) ) );
        static StackType_t xSenderTask2Stack[ configMINIMAL_STACK_SIZE * 2 ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * 2 * sizeof( StackType_t ) ) ) );


        TaskParameters_t xSenderTask1Parameters =
        {
            .pvTaskCode = prvSenderTask,
            .pcName = "1Sender",
            .usStackDepth = xBlockingStackSize,
            .pvParameters = (void*) xReceiverTaskMsgBuffers[ 0 ],
            .uxPriority = mbHIGHEST_PRIORITY,
            .puxStackBuffer = xSenderTask1Stack,
	        .xRegions        =    {
									{ ( void * ) &( ulSenderLoopCounters[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xReceiverTaskHandles[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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
									{ 0,                0,                    0                                                        }
	                            }
	    };

        TaskParameters_t xSenderTask2Parameters =
        {
            .pvTaskCode = prvSenderTask,
            .pcName = "2Sender",
            .usStackDepth = xBlockingStackSize,
            .pvParameters = (void*) xReceiverTaskMsgBuffers[ 1 ],
            .uxPriority = ( mbHIGHER_PRIORITY | portPRIVILEGE_BIT ),
            .puxStackBuffer = xSenderTask2Stack,
	        .xRegions        =    {
									{ ( void * ) &( ulSenderLoopCounters[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( pucFullBuffer[ 0 ] ), mbSHARED_MEM_SIZE_BYTES * 2,
									   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
										 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
									},
									{ ( void * ) &( xReceiverTaskHandles[ 0 ] ), mbSHARED_MEM_SIZE_BYTES,
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

        xTaskCreateRestricted( &( xSenderTask1Parameters ), NULL );
        xTaskCreateRestricted( &( xSenderTask2Parameters ), NULL );


    }
    #endif /* configSUPPORT_STATIC_ALLOCATION */

    #if ( configRUN_ADDITIONAL_TESTS == 1 )
    {
        xCoherenceTestMessageBuffer = xMessageBufferCreate( mbCOHERENCE_TEST_BUFFER_SIZE );
        configASSERT( xCoherenceTestMessageBuffer );

        xTaskCreate( prvSpaceAvailableCoherenceActor, "mbsanity1", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL );
        xTaskCreate( prvSpaceAvailableCoherenceTester, "mbsanity2", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL );
    }
    #endif
}
/*-----------------------------------------------------------*/

static void prvSingleTaskTests( MessageBufferHandle_t xMessageBuffer )
{
    size_t xReturned, xItem, xExpectedSpace, xNextLength;
    const size_t xMax6ByteMessages = mbMESSAGE_BUFFER_LENGTH_BYTES / ( 6 + mbBYTES_TO_STORE_MESSAGE_LENGTH );
    const size_t x6ByteLength = 6, x17ByteLength = 17;
    uint8_t * pucData, * pucReadData;
    TickType_t xTimeBeforeCall, xTimeAfterCall;
    const TickType_t xBlockTime = pdMS_TO_TICKS( 25 ), xAllowableMargin = pdMS_TO_TICKS( 3 );
    UBaseType_t uxOriginalPriority;

    /* Remove warning in case configASSERT() is not defined. */
    ( void ) xAllowableMargin;

    /* To minimize stack and heap usage a full size buffer is allocated from
     * the heap, then buffers which hold smaller amounts of data are overlayed
     * with the larger buffer - just make sure not to use both at once!. */
    configASSERT( pucFullBuffer );

    pucData = pucFullBuffer;
    pucReadData = pucData + x17ByteLength;

    /* Nothing has been added or removed yet, so expect the free space to be
     * exactly as created and the length of the next message to be 0. */
    xExpectedSpace = xMessageBufferSpaceAvailable( xMessageBuffer );
    configASSERT( xExpectedSpace == mbMESSAGE_BUFFER_LENGTH_BYTES );
    configASSERT( xMessageBufferIsEmpty( xMessageBuffer ) == pdTRUE );
    xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
    configASSERT( xNextLength == 0 );
    /* In case configASSERT() is not define. */
    ( void ) xExpectedSpace;
    ( void ) xNextLength;

    /* Try sending more bytes than possible, first using the FromISR version, then
     * with an infinite block time to ensure this task does not lock up. */
    xReturned = xMessageBufferSendFromISR( xMessageBuffer, ( void * ) pucData, mbMESSAGE_BUFFER_LENGTH_BYTES + sizeof( configMESSAGE_BUFFER_LENGTH_TYPE ), NULL );
    configASSERT( xReturned == ( size_t ) 0 );
    /* In case configASSERT() is not defined. */
    ( void ) xReturned;
    xReturned = xMessageBufferSend( xMessageBuffer, ( void * ) pucData, mbMESSAGE_BUFFER_LENGTH_BYTES + sizeof( configMESSAGE_BUFFER_LENGTH_TYPE ), portMAX_DELAY );
    configASSERT( xReturned == ( size_t ) 0 );
    /* In case configASSERT() is not defined. */
    ( void ) xReturned;

    /* The buffer is 50 bytes long.  When an item is added to the buffer an
     * additional 4 bytes are added to hold the item's size.  That means adding
     * 6 bytes to the buffer will actually add 10 bytes to the buffer.  Therefore,
     * with a 50 byte buffer, a maximum of 5 6 bytes items can be added before the
     * buffer is completely full.  NOTE:  The numbers in this paragraph assume
     * sizeof( configMESSAGE_BUFFER_LENGTH_TYPE ) == 4. */
    for( xItem = 0; xItem < xMax6ByteMessages; xItem++ )
    {
        configASSERT( xMessageBufferIsFull( xMessageBuffer ) == pdFALSE );

        /* Generate recognisable data to write to the buffer.  This is just
         * ascii characters that shows which loop iteration the data was written
         * in. The 'FromISR' version is used to give it some exercise as a block
         * time is not used.  That requires the call to be in a critical section
         * so this code can also run on FreeRTOS ports that do not support
         * interrupt nesting (and so don't have interrupt safe critical
         * sections).*/
        memset( ( void * ) pucData, ( ( int ) '0' ) + ( int ) xItem, x6ByteLength );
        taskENTER_CRITICAL();
        {
            xReturned = xMessageBufferSendFromISR( xMessageBuffer, ( void * ) pucData, x6ByteLength, NULL );
        }
        taskEXIT_CRITICAL();
        configASSERT( xReturned == x6ByteLength );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* The space in the buffer will have reduced by the amount of user data
         * written into the buffer and the amount of space used to store the length
         * of the data written into the buffer. */
        xExpectedSpace -= ( x6ByteLength + mbBYTES_TO_STORE_MESSAGE_LENGTH );
        xReturned = xMessageBufferSpaceAvailable( xMessageBuffer );
        configASSERT( xReturned == xExpectedSpace );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* Only 6 byte messages are written. */
        xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
        configASSERT( xNextLength == x6ByteLength );
        ( void ) xNextLength; /* In case configASSERT() is not defined. */
    }

    /* Now the buffer should be full, and attempting to add anything will should
     * fail. */
    configASSERT( xMessageBufferIsFull( xMessageBuffer ) == pdTRUE );
    xReturned = xMessageBufferSend( xMessageBuffer, ( void * ) pucData, sizeof( pucData[ 0 ] ), mbDONT_BLOCK );
    configASSERT( xReturned == 0 );
    ( void ) xReturned; /* In case configASSERT() is not defined. */

    /* Adding with a timeout should also fail after the appropriate time.  The
     * priority is temporarily boosted in this part of the test to keep the
     * allowable margin to a minimum. */
    uxOriginalPriority = uxTaskPriorityGet( NULL );
    vTaskPrioritySet( NULL, configMAX_PRIORITIES - 1 );
    xTimeBeforeCall = xTaskGetTickCount();
    xReturned = xMessageBufferSend( xMessageBuffer, ( void * ) pucData, sizeof( pucData[ 0 ] ), xBlockTime );
    xTimeAfterCall = xTaskGetTickCount();
    vTaskPrioritySet( NULL, uxOriginalPriority );
    configASSERT( ( ( TickType_t ) ( xTimeAfterCall - xTimeBeforeCall ) ) >= xBlockTime );
    configASSERT( ( ( TickType_t ) ( xTimeAfterCall - xTimeBeforeCall ) ) < ( xBlockTime + xAllowableMargin ) );
    configASSERT( xReturned == 0 );
    ( void ) xReturned; /* In case configASSERT() is not defined. */
    ( void ) xTimeBeforeCall;
    ( void ) xTimeAfterCall;

    /* The buffer is now full of data in the form "000000", "111111", etc.  Make
     * sure the data is read out as expected. */
    for( xItem = 0; xItem < xMax6ByteMessages; xItem++ )
    {
        /* Generate the data that is expected to be read out for this loop
         * iteration. */
        memset( ( void * ) pucData, ( ( int ) '0' ) + ( int ) xItem, x6ByteLength );

        /* Try reading the message into a buffer that is too small.  The message
         * should remain in the buffer. */
        xReturned = xMessageBufferReceive( xMessageBuffer, ( void * ) pucReadData, x6ByteLength - 1, mbDONT_BLOCK );
        configASSERT( xReturned == 0 );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* Should still be at least one 6 byte message still available. */
        xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
        configASSERT( xNextLength == x6ByteLength );
        ( void ) xNextLength; /* In case configASSERT() is not defined. */

        /* Read the next 6 bytes out.  The 'FromISR' version is used to give it
         * some exercise as a block time is not used.  THa requires the code to be
         * in a critical section so this test can be run with FreeRTOS ports that
         * do not support interrupt nesting (and therefore don't have interrupt
         * safe critical sections). */
        taskENTER_CRITICAL();
        {
            xReturned = xMessageBufferReceiveFromISR( xMessageBuffer, ( void * ) pucReadData, x6ByteLength, NULL );
        }
        taskEXIT_CRITICAL();
        configASSERT( xReturned == x6ByteLength );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* Does the data read out match that expected? */
        configASSERT( memcmp( ( void * ) pucData, ( void * ) pucReadData, x6ByteLength ) == 0 );

        /* The space in the buffer will have increased by the amount of user
         * data read from into the buffer and the amount of space used to store the
         * length of the data read into the buffer. */
        xExpectedSpace += ( x6ByteLength + mbBYTES_TO_STORE_MESSAGE_LENGTH );
        xReturned = xMessageBufferSpaceAvailable( xMessageBuffer );
        configASSERT( xReturned == xExpectedSpace );
        ( void ) xReturned; /* In case configASSERT() is not defined. */
    }

    /* The buffer should be empty again. */
    configASSERT( xMessageBufferIsEmpty( xMessageBuffer ) == pdTRUE );
    xExpectedSpace = xMessageBufferSpaceAvailable( xMessageBuffer );
    configASSERT( xExpectedSpace == mbMESSAGE_BUFFER_LENGTH_BYTES );
    ( void ) xExpectedSpace; /* In case configASSERT() is not defined. */
    xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
    configASSERT( xNextLength == 0 );
    ( void ) xNextLength; /* In case configASSERT() is not defined. */


    /* Reading with a timeout should also fail after the appropriate time.  The
     * priority is temporarily boosted in this part of the test to keep the
     * allowable margin to a minimum. */
    vTaskPrioritySet( NULL, configMAX_PRIORITIES - 1 );
    xTimeBeforeCall = xTaskGetTickCount();
    xReturned = xMessageBufferReceive( xMessageBuffer, ( void * ) pucReadData, x6ByteLength, xBlockTime );
    xTimeAfterCall = xTaskGetTickCount();
    vTaskPrioritySet( NULL, uxOriginalPriority );
    configASSERT( ( xTimeAfterCall - xTimeBeforeCall ) >= xBlockTime );
    configASSERT( ( xTimeAfterCall - xTimeBeforeCall ) < ( xBlockTime + xAllowableMargin ) );
    configASSERT( xReturned == 0 );
    ( void ) xReturned; /* In case configASSERT() is not defined. */
    ( void ) xTimeBeforeCall;
    ( void ) xTimeAfterCall;


    /* In the next loop 17 bytes are written to then read out on each iteration.
     * The expected length variable is always used after 17 bytes have been written
     * into the buffer - the length of the message is also written, making a total
     * of 21 bytes consumed for each 17 byte message. */
    xExpectedSpace = mbMESSAGE_BUFFER_LENGTH_BYTES - ( x17ByteLength + mbBYTES_TO_STORE_MESSAGE_LENGTH );

    /* Reading and writing 17 bytes at a time will result in 21 bytes being
     * written into the buffer, and as 50 is not divisible by 21, writing multiple
     * times will cause the data to wrap in the buffer.*/
    for( xItem = 0; xItem < 100; xItem++ )
    {
        /* Generate recognisable data to write to the queue.  This is just
         * ascii characters that shows which loop iteration the data was written
         * in. */
        memset( ( void * ) pucData, ( ( int ) '0' ) + ( int ) xItem, x17ByteLength );
        xReturned = xMessageBufferSend( xMessageBuffer, ( void * ) pucData, x17ByteLength, mbDONT_BLOCK );
        configASSERT( xReturned == x17ByteLength );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* Only 17 byte messages are written. */
        xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
        configASSERT( xNextLength == x17ByteLength );
        ( void ) xNextLength; /* In case configASSERT() is not defined. */

        /* The space in the buffer will have reduced by the amount of user data
         * written into the buffer and the amount of space used to store the length
         * of the data written into the buffer. */
        xReturned = xMessageBufferSpaceAvailable( xMessageBuffer );
        configASSERT( xReturned == xExpectedSpace );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* Read the 17 bytes out again. */
        xReturned = xMessageBufferReceive( xMessageBuffer, ( void * ) pucReadData, x17ByteLength, mbDONT_BLOCK );
        configASSERT( xReturned == x17ByteLength );
        ( void ) xReturned; /* In case configASSERT() is not defined. */

        /* Does the data read out match that expected? */
        configASSERT( memcmp( ( void * ) pucData, ( void * ) pucReadData, x17ByteLength ) == 0 );

        /* Don't expect any messages to be available as the data was read out
         * again. */
        xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
        configASSERT( xNextLength == 0 );
        ( void ) xNextLength; /* In case configASSERT() is not defined. */
    }

    /* The buffer should be empty again. */
    configASSERT( xMessageBufferIsEmpty( xMessageBuffer ) == pdTRUE );
    xExpectedSpace = xMessageBufferSpaceAvailable( xMessageBuffer );
    configASSERT( xExpectedSpace == mbMESSAGE_BUFFER_LENGTH_BYTES );

    /* Cannot write within sizeof( size_t ) (assumed to be 4 bytes in this test)
     * bytes of the full 50 bytes, as that would not leave space for the four bytes
     * taken by the data length. */
    xReturned = xMessageBufferSend( xMessageBuffer, ( const void * ) pc55ByteString, mbMESSAGE_BUFFER_LENGTH_BYTES, mbDONT_BLOCK );
    configASSERT( xReturned == 0 );
    ( void ) xReturned; /* In case configASSERT() is not defined. */
    #ifndef configMESSAGE_BUFFER_LENGTH_TYPE
    {
        /* The following will fail if configMESSAGE_BUFFER_LENGTH_TYPE is set
         * to a non 32-bit type. */
        xReturned = xMessageBufferSend( xMessageBuffer, ( const void * ) pc55ByteString, mbMESSAGE_BUFFER_LENGTH_BYTES - 1, mbDONT_BLOCK );
        configASSERT( xReturned == 0 );
        ( void ) xReturned; /* In case configASSERT() is not defined. */
        xReturned = xMessageBufferSend( xMessageBuffer, ( const void * ) pc55ByteString, mbMESSAGE_BUFFER_LENGTH_BYTES - 2, mbDONT_BLOCK );
        configASSERT( xReturned == 0 );
        ( void ) xReturned; /* In case configASSERT() is not defined. */
        xReturned = xMessageBufferSend( xMessageBuffer, ( const void * ) pc55ByteString, mbMESSAGE_BUFFER_LENGTH_BYTES - 3, mbDONT_BLOCK );
        configASSERT( xReturned == 0 );
        ( void ) xReturned; /* In case configASSERT() is not defined. */
    }
    #endif /* ifndef configMESSAGE_BUFFER_LENGTH_TYPE */

    /* Don't expect any messages to be available as the above were too large to
     * get written. */
    xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
    configASSERT( xNextLength == 0 );
    ( void ) xNextLength; /* In case configASSERT() is not defined. */

    /* Can write mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ) bytes though. */
    xReturned = xMessageBufferSend( xMessageBuffer, ( const void * ) pc55ByteString, mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ), mbDONT_BLOCK );
    configASSERT( xReturned == mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ) );
    ( void ) xReturned; /* In case configASSERT() is not defined. */
    xNextLength = xMessageBufferNextLengthBytes( xMessageBuffer );
    configASSERT( xNextLength == ( mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ) ) );
    ( void ) xNextLength; /* In case configASSERT() is not defined. */
    xReturned = xMessageBufferReceive( xMessageBuffer, ( void * ) pucFullBuffer, mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ), mbDONT_BLOCK );
    configASSERT( xReturned == ( mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ) ) );
    ( void ) xReturned; /* In case configASSERT() is not defined. */
    configASSERT( memcmp( ( const void * ) pucFullBuffer, pc55ByteString, mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ) ) == 0 );

    /* Clean up. */
    memset( ( void * ) pucFullBuffer, ( ( int ) '0' ) , sizeof( pucFullBuffer ) );
    xMessageBufferReset( xMessageBuffer );
}
/*-----------------------------------------------------------*/

static void prvNonBlockingSenderTask( void * pvParameters )
{
    MessageBufferHandle_t xMessageBuffer;
    int32_t iDataToSend = 0;
    size_t xStringLength;
    const int32_t iMaxValue = 1500;
    char cTxString[ 12 ]; /* Large enough to hold a 32 number in ASCII. */

    /* In this case the message buffer has already been created and is passed
     * into the task using the task's parameter. */

    xMessageBuffer = ( MessageBufferHandle_t ) pvParameters;

    /* Create a string from an incrementing number.  The length of the
     * string will increase and decrease as the value of the number increases
     * then overflows. */
    memset( cTxString, 0x00, sizeof( cTxString ) );
    itoa(iDataToSend, cTxString, sizeof( cTxString ));
    xStringLength = strlen( cTxString );

    for( ; ; )
    {
        /* Doesn't block so calls can interleave with the non-blocking
         * receives performed by prvNonBlockingReceiverTask(). */
        if( xMessageBufferSend( xMessageBuffer, ( void * ) cTxString, strlen( cTxString ), mbDONT_BLOCK ) == xStringLength )
        {
            iDataToSend++;

            if( iDataToSend > iMaxValue )
            {
                /* The value sent is reset back to 0 to ensure the string being sent
                 * does not remain at the same length for too long. */
                iDataToSend = 0;
            }

            /* Create the next string. */
            memset( cTxString, 0x00, sizeof( cTxString ) );
            itoa(iDataToSend, cTxString, sizeof( cTxString ));
            xStringLength = strlen( cTxString );
        }
    }
}
/*-----------------------------------------------------------*/

static void prvNonBlockingReceiverTask( void * pvParameters )
{
    MessageBufferHandle_t xMessageBuffer;
    BaseType_t xNonBlockingReceiveError = pdFALSE;
    int32_t iDataToSend = 0;
    size_t xStringLength, xReceiveLength;
    const int32_t iMaxValue = 1500;
    char cExpectedString[ 12 ]; /* Large enough to hold a 32 number in ASCII. */
    char cRxString[ 12 ];

    /* In this case the message buffer has already been created and is passed
     * into the task using the task's parameter. */
    xMessageBuffer = ( MessageBufferHandle_t ) pvParameters;

    /* Create a string from an incrementing number.  The length of the
     * string will increase and decrease as the value of the number increases
     * then overflows.  This should always match the string sent to the buffer by
     * the non blocking sender task. */
    memset( cExpectedString, 0x00, sizeof( cExpectedString ) );
    memset( cRxString, 0x00, sizeof( cRxString ) );
    itoa( iDataToSend, cExpectedString, sizeof(cExpectedString) );
    xStringLength = strlen( cExpectedString );

    for( ; ; )
    {
        /* Doesn't block so calls can interleave with the non-blocking
         * receives performed by prvNonBlockingReceiverTask(). */
        xReceiveLength = xMessageBufferReceive( xMessageBuffer, ( void * ) cRxString, sizeof( cRxString ), mbDONT_BLOCK );

        /* Should only ever receive no data is available, or the expected
         * length of data is available. */
        if( ( xReceiveLength != 0 ) && ( xReceiveLength != xStringLength ) )
        {
            xNonBlockingReceiveError = pdTRUE;
        }

        if( xReceiveLength == xStringLength )
        {
            /* Ensure the received data was that expected, then generate the
             * next expected string. */
            if( strcmp( cRxString, cExpectedString ) != 0 )
            {
                xNonBlockingReceiveError = pdTRUE;
            }

            iDataToSend++;

            if( iDataToSend > iMaxValue )
            {
                /* The value sent is reset back to 0 to ensure the string being sent
                 * does not remain at the same length for too long. */
                iDataToSend = 0;
            }

            memset( cExpectedString, 0x00, sizeof( cExpectedString ) );
            memset( cRxString, 0x00, sizeof( cRxString ) );
            itoa( iDataToSend, cExpectedString, sizeof(cExpectedString) );
            xStringLength = strlen( cExpectedString );

            if( xNonBlockingReceiveError == pdFALSE )
            {
                /* No errors detected so increment the counter that lets the
                 *  check task know this test is still functioning correctly. */
                ulNonBlockingRxCounter[ 0 ]++;
            }
        }
    }
}
/*-----------------------------------------------------------*/

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

    static void prvSenderTask( void * pvParameters )
    {
        MessageBufferHandle_t xMessageBuffer, xTempMessageBuffer;
        int32_t iDataToSend = 0;
        const int32_t iSendsBetweenIncrements = 100;
        char cTxString[ 12 ]; /* Large enough to hold a 32 number in ASCII. */
        const TickType_t xTicksToWait = mbRX_TX_BLOCK_TIME, xShortDelay = pdMS_TO_TICKS( 50 );
        StaticMessageBuffer_t xStaticMessageBuffer;
        size_t xBytesSent;


        /* The task's priority is used as an index into the loop counters used to
         * indicate this task is still running. */
        UBaseType_t uxIndex = uxTaskPriorityGet( NULL );

        /* Make sure a change in priority does not inadvertently result in an
         * invalid array index. */
        configASSERT( uxIndex < mbNUMBER_OF_ECHO_CLIENTS + 1 );

        /* Avoid compiler warnings about unused parameters. */

        xMessageBuffer = ( MessageBufferHandle_t )pvParameters;

        /* Now the message buffer has been created the receiver task can be created.
         * If this sender task has the higher priority then the receiver task is
         * created at the lower priority - if this sender task has the lower priority
         * then the receiver task is created at the higher priority. */
        if( uxTaskPriorityGet( NULL ) == mbHIGHER_PRIORITY )
        {
            /* Here prvSingleTaskTests() performs various tests on a message buffer
             * that was created statically. */
            prvSingleTaskTests( xMessageBuffer );
        	vTaskPrioritySet( xReceiverTaskHandles[ 1 ], mbHIGHEST_PRIORITY );
        	vTaskResume( xReceiverTaskHandles[ 1 ] );
        }
        else
        {

        	vTaskPrioritySet( xReceiverTaskHandles[ 0 ], mbHIGHER_PRIORITY );
        	vTaskResume( xReceiverTaskHandles[ 0 ] );
        }

        for( ; ; )
        {
            /* Create a string from an incrementing number.  The length of the
             * string will increase and decrease as the value of the number increases
             * then overflows. */
            memset( cTxString, 0x00, sizeof( cTxString ) );
            itoa(iDataToSend, cTxString, sizeof( cTxString ));

            do
            {
                xBytesSent = xMessageBufferSend( xMessageBuffer, ( void * ) cTxString, strlen( cTxString ), xTicksToWait );
            } while( xBytesSent == 0 ); /* Buffer may become full when receiver is running at the idle priority. */

            iDataToSend++;

            if( ( iDataToSend % iSendsBetweenIncrements ) == 0 )
            {
                /* Increment a loop counter so a check task can tell this task is
                 * still running as expected. */

            	ulSenderLoopCounters[ uxIndex ]++;


                if( uxTaskPriorityGet( NULL ) == mbHIGHEST_PRIORITY )
                {
                    /* Allow other tasks to run. */
                    vTaskDelay( xShortDelay );
                }

                /* This message buffer is just created and deleted to ensure no
                 * issues when attempting to delete a message buffer that was
                 * created using statically allocated memory.  To save stack space
                 * the buffer is set to point to the cTxString array - this is
                 * ok because nothing is actually written to the memory. */
                xTempMessageBuffer = xMessageBufferCreateStatic( sizeof( cTxString ), ( uint8_t * ) cTxString, &xStaticMessageBuffer );
                xMessageBufferReset( xTempMessageBuffer );
                vMessageBufferDelete( xTempMessageBuffer );
            }
        }
    }

#endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

    static void prvReceiverTask( void * pvParameters )
    {
        MessageBufferHandle_t const pxMessageBuffer = ( MessageBufferHandle_t ) pvParameters;
        char cExpectedString[ 12 ]; /* Large enough to hold a 32-bit number in ASCII. */
        char cReceivedString[ 12 ]; /* Large enough to hold a 32-bit number in ASCII. */
        int32_t iExpectedData = 0;
        const TickType_t xTicksToWait = pdMS_TO_TICKS( 5UL );
        size_t xReceivedBytes;

        for( ; ; )
        {
            /* Generate the next expected string in the cExpectedString buffer. */
            memset( cExpectedString, 0x00, sizeof( cExpectedString ) );
            itoa(iExpectedData, cExpectedString, sizeof( cExpectedString ));

            /* Receive the next string from the message buffer. */
            memset( cReceivedString, 0x00, sizeof( cReceivedString ) );

            do
            {
                xReceivedBytes = xMessageBufferReceive( pxMessageBuffer, ( void * ) cReceivedString, sizeof( cExpectedString ), xTicksToWait );
            } while( xReceivedBytes == 0 );

            /* Ensure the received string matches the expected string. */
            configASSERT( strcmp( cExpectedString, cReceivedString ) == 0 );

            iExpectedData++;
        }
    }

#endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

static void prvEchoClient( void * pvParameters )
{
    size_t xSendLength = 0, ux;
    char * pcStringToSend, * pcStringReceived, cNextChar = mbASCII_SPACE;
    const TickType_t xTicksToWait = pdMS_TO_TICKS( 50 );

/* The task's priority is used as an index into the loop counters used to
 * indicate this task is still running. */
    UBaseType_t uxIndex = uxTaskPriorityGet( NULL );

    if( uxIndex == mbHIGHER_PRIORITY )
    {
    	pcStringToSend = pcStringToSend1;
    	pcStringReceived = pcStringReceived1;
    }
    else
    {
    	pcStringToSend = pcStringToSend2;
    	pcStringReceived = pcStringReceived2;
    }

/* Pointers to the client and server message buffers are passed into this task
 * using the task's parameter. */
    EchoMessageBuffers_t * pxMessageBuffers = ( EchoMessageBuffers_t * ) pvParameters;


    /* Create the buffer into which strings to send to the server will be
     * created, and the buffer into which strings echoed back from the server will
     * be copied. */
    //pcStringToSend = ( char * ) pvPortMalloc( mbMESSAGE_BUFFER_LENGTH_BYTES );
    //pcStringReceived = ( char * ) pvPortMalloc( mbMESSAGE_BUFFER_LENGTH_BYTES );

    configASSERT( pcStringToSend );
    configASSERT( pcStringReceived );

    for( ; ; )
    {
        /* Generate the length of the next string to send. */
        xSendLength++;

        /* The message buffer is being used to hold variable length data, so
         * each data item requires sizeof( size_t ) bytes to hold the data's
         * length, hence the sizeof() in the if() condition below. */
        if( xSendLength > ( mbMESSAGE_BUFFER_LENGTH_BYTES - sizeof( size_t ) ) )
        {
            /* Back to a string length of 1. */
            xSendLength = sizeof( char );

            /* Maintain a count of the number of times this code executes so a
             * check task can determine if this task is still functioning as
             * expected or not.  As there are two client tasks, and the priorities
             * used are 0 and 1, the task's priority is used as an index into the
             * loop count array. */
            ulEchoLoopCounters[ uxIndex ]++;
        }

        memset( pcStringToSend, 0x00, mbMESSAGE_BUFFER_LENGTH_BYTES );

        for( ux = 0; ux < xSendLength; ux++ )
        {
            pcStringToSend[ ux ] = cNextChar;

            cNextChar++;

            if( cNextChar > mbASCII_TILDA )
            {
                cNextChar = mbASCII_SPACE;
            }
        }

        /* Send the generated string to the buffer. */
        do
        {
            ux = xMessageBufferSend( pxMessageBuffers->xEchoClientBuffer, ( void * ) pcStringToSend, xSendLength, xTicksToWait );

            if( ux == 0 )
            {
                mtCOVERAGE_TEST_MARKER();
            }
        } while( ux == 0 );

        /* Wait for the string to be echoed back. */
        memset( pcStringReceived, 0x00, mbMESSAGE_BUFFER_LENGTH_BYTES );
        xMessageBufferReceive( pxMessageBuffers->xEchoServerBuffer, ( void * ) pcStringReceived, xSendLength, portMAX_DELAY );

        configASSERT( strcmp( pcStringToSend, pcStringReceived ) == 0 );
    }
}
/*-----------------------------------------------------------*/

static void prvEchoServer( void * pvParameters )
{
    MessageBufferHandle_t xTempMessageBuffer;
    size_t xReceivedLength;
    char * pcReceivedString;
    EchoMessageBuffers_t * pxMessageBuffers = ( EchoMessageBuffers_t * ) pvParameters;
    TickType_t xTimeOnEntering;
    const TickType_t xTicksToBlock = pdMS_TO_TICKS( 250UL );


    configASSERT( pxMessageBuffers->xEchoClientBuffer );
    configASSERT( pxMessageBuffers->xEchoServerBuffer );

    /*Check the priority of the task and then allocate local buffer based on the priority*/
    UBaseType_t uxIndex = uxTaskPriorityGet( NULL );
    if( uxIndex == mbHIGHER_PRIORITY)
    {
    	pcReceivedString = pcReceivedString1;
    }
    else
    {
    	pcReceivedString = pcReceivedString2;
    }
    configASSERT( pcReceivedString );

    /* Suspend both the client tasks */
    vTaskSuspend( xEchoClientTaskHandle[ 0 ] );
    vTaskSuspend( xEchoClientTaskHandle[ 1 ] );

    /* Don't expect to receive anything yet! */
    xTimeOnEntering = xTaskGetTickCount();
    xReceivedLength = xMessageBufferReceive( pxMessageBuffers->xEchoClientBuffer, ( void * ) pcReceivedString, mbMESSAGE_BUFFER_LENGTH_BYTES, xTicksToBlock );
    configASSERT( ( ( TickType_t ) ( xTaskGetTickCount() - xTimeOnEntering ) ) >= xTicksToBlock );
    configASSERT( xReceivedLength == 0 );

    /* Now the message buffers have been created the echo client task can be
     * created.  If this server task has the higher priority then the client task
     * is created at the lower priority - if this server task has the lower
     * priority then the client task is created at the higher priority. */
    if( uxTaskPriorityGet( NULL ) == mbHIGHER_PRIORITY )
    {
    	vTaskPrioritySet( xEchoClientTaskHandle[ 1 ], mbHIGHEST_PRIORITY );
    	vTaskResume( xEchoClientTaskHandle[ 1 ] );
    }
    else
    {
        /* Here prvSingleTaskTests() performs various tests on a message buffer
         * that was created dynamically. */
        prvSingleTaskTests( pxMessageBuffers->xEchoClientBuffer );
    	vTaskPrioritySet( xEchoClientTaskHandle[ 0 ], mbHIGHER_PRIORITY );
    	vTaskResume( xEchoClientTaskHandle[ 0 ] );
    }

    for( ; ; )
    {
        memset( pcReceivedString, 0x00, mbMESSAGE_BUFFER_LENGTH_BYTES );

        /* Has any data been sent by the client? */
        xReceivedLength = xMessageBufferReceive( pxMessageBuffers->xEchoClientBuffer, ( void * ) pcReceivedString, mbMESSAGE_BUFFER_LENGTH_BYTES, portMAX_DELAY );

        /* Should always receive data as max delay was used. */
        configASSERT( xReceivedLength > 0 );

        /* Echo the received data back to the client. */
        xMessageBufferSend( pxMessageBuffers->xEchoServerBuffer, ( void * ) pcReceivedString, xReceivedLength, portMAX_DELAY );

        /* This message buffer is just created and deleted to ensure no memory
         * leaks. */
        xTempMessageBuffer = xMessageBufferCreate( mbMESSAGE_BUFFER_LENGTH_BYTES );
        vMessageBufferDelete( xTempMessageBuffer );
    }
}
/*-----------------------------------------------------------*/

/* Tests within configRUN_ADDITIONAL_TESTS blocks only execute on larger
 * platforms or have been added to pre-existing files that are already in use
 * by other test projects without ensuring they don't cause those pre-existing
 * projects to run out of program or data memory. */
#if ( configRUN_ADDITIONAL_TESTS == 1 )

    static void prvSpaceAvailableCoherenceActor( void * pvParameters )
    {
        static char * cTxString = "12345";
        char cRxString[ mbCOHERENCE_TEST_BYTES_WRITTEN + 1 ]; /* +1 for NULL terminator. */

        ( void ) pvParameters;

        for( ; ; )
        {
            /* Add bytes to the buffer so the other task should see
             * mbEXPECTED_FREE_BYTES_AFTER_WRITING_STRING bytes free. */
            xMessageBufferSend( xCoherenceTestMessageBuffer, ( void * ) cTxString, strlen( cTxString ), 0 );
            configASSERT( xMessageBufferSpacesAvailable( xCoherenceTestMessageBuffer ) == mbEXPECTED_FREE_BYTES_AFTER_WRITING_STRING );

            /* Read out message again so the other task should read the full
             * mbCOHERENCE_TEST_BUFFER_SIZE bytes free again. */
            memset( ( void * ) cRxString, 0x00, sizeof( cRxString ) );
            xMessageBufferReceive( xCoherenceTestMessageBuffer, ( void * ) cRxString, mbCOHERENCE_TEST_BYTES_WRITTEN, 0 );
            configASSERT( strcmp( cTxString, cRxString ) == 0 );
        }
    }
    /*-----------------------------------------------------------*/

    static void prvSpaceAvailableCoherenceTester( void * pvParameters )
    {
        size_t xSpaceAvailable;
        BaseType_t xErrorFound = pdFALSE;

        ( void ) pvParameters;

        for( ; ; )
        {
            /* This message buffer is only ever empty or contains 5 bytes.  So all
             * queries of its free space should result in one of the two values tested
             * below. */
            xSpaceAvailable = xMessageBufferSpacesAvailable( xCoherenceTestMessageBuffer );

            if( ( xSpaceAvailable == mbCOHERENCE_TEST_BUFFER_SIZE ) ||
                ( xSpaceAvailable == mbEXPECTED_FREE_BYTES_AFTER_WRITING_STRING ) )
            {
                /* Only continue to increment the variable that shows this task
                 * is still executing if no errors have been found. */
                if( xErrorFound == pdFALSE )
                {
                    ulSizeCoherencyTestCycles++;
                }
            }
            else
            {
                xErrorFound = pdTRUE;
            }

            configASSERT( xErrorFound == pdFALSE );
        }
    }

#endif /* configRUN_ADDITIONAL_TESTS == 1  */
/*-----------------------------------------------------------*/

BaseType_t xAreMessageBufferTasksStillRunning( void )
{
    static uint32_t ulLastEchoLoopCounters[ mbNUMBER_OF_ECHO_CLIENTS ] = { 0 };
    static uint32_t ulLastNonBlockingRxCounter = 0;
    BaseType_t xReturn = pdPASS, x;

    for( x = 0; x < mbNUMBER_OF_ECHO_CLIENTS; x++ )
    {
        if( ulLastEchoLoopCounters[ x ] == ulEchoLoopCounters[ x + 1 ] )
        {
            xReturn = pdFAIL;
        }
        else
        {
            ulLastEchoLoopCounters[ x ] = ulEchoLoopCounters[ x + 1 ];
        }
    }

    if( ulNonBlockingRxCounter[ 0 ] == ulLastNonBlockingRxCounter )
    {
        xReturn = pdFAIL;
    }
    else
    {
        ulLastNonBlockingRxCounter = ulNonBlockingRxCounter[ 0 ];
    }

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    {
        static uint32_t ulLastSenderLoopCounters[ mbNUMBER_OF_ECHO_CLIENTS ] = { 0 };

        for( x = 0; x < mbNUMBER_OF_SENDER_TASKS; x++ )
        {
            if( ulLastSenderLoopCounters[ x ] == ulSenderLoopCounters[ x + 1 ] )
            {
                xReturn = pdFAIL;
            }
            else
            {
                ulLastSenderLoopCounters[ x ] = ulSenderLoopCounters[ x + 1 ];
            }
        }
    }
    #endif /* configSUPPORT_STATIC_ALLOCATION */

    #if ( configRUN_ADDITIONAL_TESTS == 1 )
    {
        static uint32_t ullastSizeCoherencyTestCycles = 0UL;

        if( ullastSizeCoherencyTestCycles == ulSizeCoherencyTestCycles )
        {
            xReturn = pdFAIL;
        }
        else
        {
            ullastSizeCoherencyTestCycles = ulSizeCoherencyTestCycles;
        }
    }
    #endif /* if ( configRUN_ADDITIONAL_TESTS == 1 ) */

    return xReturn;
}
/*-----------------------------------------------------------*/

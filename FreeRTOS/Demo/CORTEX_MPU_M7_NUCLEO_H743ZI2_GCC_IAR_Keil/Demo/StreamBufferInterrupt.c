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
 * A simple example that shows a stream buffer being used to pass data from an
 * interrupt to a task.
 *
 * There are two strings, pcStringToSend and pcStringToReceive, where
 * pcStringToReceive is a substring of pcStringToSend.  The interrupt sends
 * a few bytes of pcStringToSend to a stream buffer ever few times that it
 * executes.  A task reads the bytes from the stream buffer, looking for the
 * substring, and flagging an error if the received data is invalid.
 */

/* Standard includes. */
#include "stdio.h"
#include "string.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

/* Demo app includes. */
#include "StreamBufferInterrupt.h"

#define sbiSTREAM_BUFFER_LENGTH_BYTES        ( ( size_t ) 100 )
#define sbiSTREAM_BUFFER_TRIGGER_LEVEL_10    ( ( BaseType_t ) 10 )


#define streambufferSHARED_MEM_SIZE_WORDS             ( 8 )
#define streambufferSHARED_MEM_SIZE_BYTES             ( 32 )

/*-----------------------------------------------------------*/

/* Implements the task that receives a stream of bytes from the interrupt. */
static void prvReceivingTask( void * pvParameters );

/*-----------------------------------------------------------*/

/* The stream buffer that is used to send data from an interrupt to the task. */
static StreamBufferHandle_t xStreamBuffer[ streambufferSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( streambufferSHARED_MEM_SIZE_BYTES ) ) ) = { NULL };

/* The string that is sent from the interrupt to the task four bytes at a
 * time.  Must be multiple of 4 bytes long as the ISR sends 4 bytes at a time*/
static const char * pcStringToSend[ streambufferSHARED_MEM_SIZE_BYTES ] __attribute__( ( aligned( streambufferSHARED_MEM_SIZE_BYTES ) ) ) = { "_____Hello FreeRTOS_____" };

/* The string to task is looking for, which must be a substring of
 * pcStringToSend. */
static const char * pcStringToReceive[ streambufferSHARED_MEM_SIZE_BYTES ] __attribute__( ( aligned( streambufferSHARED_MEM_SIZE_BYTES ) ) ) = { "Hello FreeRTOS" };

/* Set to pdFAIL if anything unexpected happens. */
static BaseType_t xDemoStatus[ streambufferSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( streambufferSHARED_MEM_SIZE_BYTES ) ) ) = { pdPASS };

/* Incremented each time pcStringToReceive is correctly received, provided no
 * errors have occurred.  Used so the check task can check this task is still
 * running as expected. */
static uint32_t ulCycleCount[ streambufferSHARED_MEM_SIZE_WORDS ] __attribute__( ( aligned( streambufferSHARED_MEM_SIZE_BYTES ) ) ) = { 0 };;

/*-----------------------------------------------------------*/

void vStartStreamBufferInterruptDemo( void )
{
    /* Create the stream buffer that sends data from the interrupt to the
     * task, and create the task. */
    xStreamBuffer[ 0 ] = xStreamBufferCreate( /* The buffer length in bytes. */
        sbiSTREAM_BUFFER_LENGTH_BYTES,
        /* The stream buffer's trigger level. */
        sbiSTREAM_BUFFER_TRIGGER_LEVEL_10 );

    static StackType_t xReceivingTaskStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );

    TaskParameters_t xReceivingTaskParameters =
    {
        .pvTaskCode      = prvReceivingTask,
        .pcName          = "StrIntRx",
        .usStackDepth    = configMINIMAL_STACK_SIZE,
        .pvParameters    = NULL,
        .uxPriority      = tskIDLE_PRIORITY + 2,
        .puxStackBuffer  = xReceivingTaskStack,
        .xRegions        =    {
								{ ( void * ) &( xDemoStatus[ 0 ] ), streambufferSHARED_MEM_SIZE_BYTES,
								   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
									 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
								},
								{ ( void * ) &( ulCycleCount[ 0 ] ), streambufferSHARED_MEM_SIZE_BYTES,
								   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
									 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
								},
								{ ( void * ) &( pcStringToReceive[ 0 ] ), streambufferSHARED_MEM_SIZE_BYTES,
								   ( portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER |
									 ( ( configTEX_S_C_B_SRAM & portMPU_RASR_TEX_S_C_B_MASK ) << portMPU_RASR_TEX_S_C_B_LOCATION ) )
								},
								{ ( void * ) &( xStreamBuffer[ 0 ] ), streambufferSHARED_MEM_SIZE_BYTES,
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

    xTaskCreateRestricted( &( xReceivingTaskParameters ), NULL );

}
/*-----------------------------------------------------------*/

static void prvReceivingTask( void * pvParameters )
{
    char cRxBuffer[ 20 ];
    BaseType_t xNextByte = 0;

    /* Remove warning about unused parameters. */
    ( void ) pvParameters;

    /* Make sure the string will fit in the Rx buffer, including the NULL
     * terminator. */
    configASSERT( sizeof( cRxBuffer ) > strlen( pcStringToReceive[ 0 ] ) );

    /* Make sure the stream buffer has been created. */
    configASSERT( xStreamBuffer[ 0 ] != NULL );

    /* Start with the Rx buffer in a known state. */
    memset( cRxBuffer, 0x00, sizeof( cRxBuffer ) );

    for( ; ; )
    {
        /* Keep receiving characters until the end of the string is received.
         * Note:  An infinite block time is used to simplify the example.  Infinite
         * block times are not recommended in production code as they do not allow
         * for error recovery. */
        xStreamBufferReceive( /* The stream buffer data is being received from. */
            xStreamBuffer[ 0 ],
            /* Where to place received data. */
            ( void * ) &( cRxBuffer[ xNextByte ] ),
            /* The number of bytes to receive. */
            sizeof( char ),

            /* The time to wait for the next data if the buffer
             * is empty. */
            portMAX_DELAY );

        /* If xNextByte is 0 then this task is looking for the start of the
         * string, which is 'H'. */
        if( xNextByte == 0 )
        {
            if( cRxBuffer[ xNextByte ] == 'H' )
            {
                /* The start of the string has been found.  Now receive
                 * characters until the end of the string is found. */
                xNextByte++;
            }
        }
        else
        {
            /* Receiving characters while looking for the end of the string,
             * which is an 'S'. */
            if( cRxBuffer[ xNextByte ] == 'S' )
            {
                /* The string has now been received.  Check its validity. */
                if( strcmp( cRxBuffer, pcStringToReceive[ 0 ] ) != 0 )
                {
                    xDemoStatus[ 0 ] = pdFAIL;
                }

                /* Return to start looking for the beginning of the string
                 * again. */
                memset( cRxBuffer, 0x00, sizeof( cRxBuffer ) );
                xNextByte = 0;

                /* Increment the cycle count as an indication to the check task
                 * that this demo is still running. */
                if( xDemoStatus[ 0 ] == pdPASS )
                {
                    ulCycleCount[ 0 ]++;
                }
            }
            else
            {
                /* Receive the next character the next time around, while
                 * continuing to look for the end of the string. */
                xNextByte++;

                configASSERT( ( size_t ) xNextByte < sizeof( cRxBuffer ) );
            }
        }
    }
}
/*-----------------------------------------------------------*/

void vBasicStreamBufferSendFromISR( void )
{
    static size_t xNextByteToSend = 0;
    const BaseType_t xCallsBetweenSends = 100, xBytesToSend = 4;
    static BaseType_t xCallCount = 0;

    /* Is it time to write to the stream buffer again? */
    xCallCount++;

    if( xCallCount > xCallsBetweenSends )
    {
        xCallCount = 0;

        /* Send the next four bytes to the stream buffer. */
        xStreamBufferSendFromISR( xStreamBuffer[ 0 ],
                                  ( const void * ) ( pcStringToSend[ 0 ] + xNextByteToSend ),
                                  xBytesToSend,
                                  NULL );

        /* Send the next four bytes the next time around, wrapping to the start
         * of the string if necessary. */
        xNextByteToSend += xBytesToSend;

        if( xNextByteToSend >= strlen( pcStringToSend[ 0 ] ) )
        {
            xNextByteToSend = 0;
        }
    }
}
/*-----------------------------------------------------------*/

BaseType_t xIsInterruptStreamBufferDemoStillRunning( void )
{
    uint32_t ulLastCycleCount = 0;

    /* Check the demo is still running. */
    if( ulLastCycleCount == ulCycleCount[ 0 ] )
    {
        xDemoStatus[ 0 ] = pdFAIL;
    }
    else
    {
        ulLastCycleCount = ulCycleCount[ 0 ];
    }

    return xDemoStatus[ 0 ];
}
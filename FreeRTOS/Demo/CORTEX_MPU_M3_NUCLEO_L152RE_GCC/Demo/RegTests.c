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

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"

/* Interface include files. */
#include "RegTests.h"

#ifndef REGISTER_TEST_TASK_STACK_SIZE
    #define REGISTER_TEST_TASK_STACK_SIZE    configMINIMAL_STACK_SIZE
#endif

/* Tasks that implement register tests. */
static void prvRegisterTest1Task( void *pvParameters );
static void prvRegisterTest2Task( void *pvParameters );
static void prvRegisterTest3Task( void *pvParameters );
static void prvRegisterTest4Task( void *pvParameters );

/* Functions implemented in assembly. */
void vRegTest1Asm( void ) __attribute__( ( naked ) );
void vRegTest2Asm( void ) __attribute__( ( naked ) );
void vRegTest3Asm( void ) __attribute__( ( naked ) );
void vRegTest4Asm( void ) __attribute__( ( naked ) );

/* Flag that will be latched to pdTRUE should any unexpected behaviour be
detected in any of the tasks. */
static volatile BaseType_t xErrorDetected = pdFALSE;

/* Counters that are incremented on each cycle of a test.  This is used to
detect a stalled task - a test that is no longer running. */
volatile uint32_t ulRegisterTest1Counter = 0;
volatile uint32_t ulRegisterTest2Counter = 0;
volatile uint32_t ulRegisterTest3Counter = 0;
volatile uint32_t ulRegisterTest4Counter = 0;
/*-----------------------------------------------------------*/

static void prvRegisterTest1Task( void *pvParameters )
{
	( void ) pvParameters;

	for( ; ; )
	{
		vRegTest1Asm();
	}
}
/*-----------------------------------------------------------*/

static void prvRegisterTest2Task( void *pvParameters )
{
	( void ) pvParameters;

	for( ; ; )
	{
		vRegTest2Asm();
	}
}
/*-----------------------------------------------------------*/

static void prvRegisterTest3Task( void *pvParameters )
{
	( void ) pvParameters;

	for( ; ; )
	{
		vRegTest3Asm();
	}
}
/*-----------------------------------------------------------*/

static void prvRegisterTest4Task( void *pvParameters )
{
	( void ) pvParameters;

	for( ; ; )
	{
		vRegTest4Asm();
	}
}
/*-----------------------------------------------------------*/

void vStartRegisterTasks( UBaseType_t uxPriority )
{
	BaseType_t ret;
	static StackType_t xRegisterTest1TaskStack[ REGISTER_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( REGISTER_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
	static StackType_t xRegisterTest2TaskStack[ REGISTER_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( REGISTER_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
	static StackType_t xRegisterTest3TaskStack[ REGISTER_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( REGISTER_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );
	static StackType_t xRegisterTest4TaskStack[ REGISTER_TEST_TASK_STACK_SIZE ] __attribute__( ( aligned( REGISTER_TEST_TASK_STACK_SIZE * sizeof( StackType_t ) ) ) );

	TaskParameters_t xRegisterTest1TaskParameters =
	{
		.pvTaskCode		= prvRegisterTest1Task,
		.pcName			= "Reg Tst 1",
		.usStackDepth	= REGISTER_TEST_TASK_STACK_SIZE,
		.pvParameters	= NULL,
		.uxPriority		= ( uxPriority | portPRIVILEGE_BIT ),
		.puxStackBuffer	= xRegisterTest1TaskStack,
		.xRegions		=
		{
			{ 0, 0, 0 },
			{ 0, 0, 0 },
			{ 0, 0, 0 }
		}
	};
	TaskParameters_t xRegisterTest2TaskParameters =
	{
		.pvTaskCode		= prvRegisterTest2Task,
		.pcName			= "Reg Tst 2",
		.usStackDepth	= REGISTER_TEST_TASK_STACK_SIZE,
		.pvParameters	= NULL,
		.uxPriority		= ( uxPriority | portPRIVILEGE_BIT ),
		.puxStackBuffer	= xRegisterTest2TaskStack,
		.xRegions		=
		{
			{ 0, 0, 0 },
			{ 0, 0, 0 },
			{ 0, 0, 0 }
		}
	};
	TaskParameters_t xRegisterTest3TaskParameters =
	{
		.pvTaskCode		= prvRegisterTest3Task,
		.pcName			= "Reg Tst 3",
		.usStackDepth	= REGISTER_TEST_TASK_STACK_SIZE,
		.pvParameters	= NULL,
		.uxPriority		= ( uxPriority | portPRIVILEGE_BIT ),
		.puxStackBuffer	= xRegisterTest3TaskStack,
		.xRegions		=
		{
			{ 0, 0, 0 },
			{ 0, 0, 0 },
			{ 0, 0, 0 }
		}
	};
	TaskParameters_t xRegisterTest4TaskParameters =
	{
		.pvTaskCode		= prvRegisterTest4Task,
		.pcName			= "Reg Tst 4",
		.usStackDepth	= REGISTER_TEST_TASK_STACK_SIZE,
		.pvParameters	= NULL,
		.uxPriority		= ( uxPriority | portPRIVILEGE_BIT ),
		.puxStackBuffer	= xRegisterTest4TaskStack,
		.xRegions		=
		{
			{ 0, 0, 0 },
			{ 0, 0, 0 },
			{ 0, 0, 0 }
		}
	};

	ret = xTaskCreateRestricted( &( xRegisterTest1TaskParameters ), NULL );
	configASSERT( ret == pdPASS );

	ret = xTaskCreateRestricted( &( xRegisterTest2TaskParameters ), NULL );
	configASSERT( ret == pdPASS );

	ret = xTaskCreateRestricted( &( xRegisterTest3TaskParameters ), NULL );
	configASSERT( ret == pdPASS );

	ret = xTaskCreateRestricted( &( xRegisterTest4TaskParameters ), NULL );
	configASSERT( ret == pdPASS );
}
/*-----------------------------------------------------------*/

BaseType_t xAreRegisterTasksStillRunning( void )
{
static uint32_t ulLastRegisterTest1Counter = 0, ulLastRegisterTest2Counter = 0;
static uint32_t ulLastRegisterTest3Counter = 0, ulLastRegisterTest4Counter = 0;

	/* If the register test task is still running then we expect the loop
	 * counters to have incremented since this function was last called. */
	if( ulLastRegisterTest1Counter == ulRegisterTest1Counter )
	{
		xErrorDetected = pdTRUE;
	}

	if( ulLastRegisterTest2Counter == ulRegisterTest2Counter )
	{
		xErrorDetected = pdTRUE;
	}

	if( ulLastRegisterTest3Counter == ulRegisterTest3Counter )
	{
		xErrorDetected = pdTRUE;
	}

	if( ulLastRegisterTest4Counter == ulRegisterTest4Counter )
	{
		xErrorDetected = pdTRUE;
	}

	ulLastRegisterTest1Counter = ulRegisterTest1Counter;
	ulLastRegisterTest2Counter = ulRegisterTest2Counter;
	ulLastRegisterTest3Counter = ulRegisterTest3Counter;
	ulLastRegisterTest4Counter = ulRegisterTest4Counter;

	/* Errors detected in the task itself will have latched xErrorDetected
	 * to true. */
	return ( BaseType_t ) !xErrorDetected;
}
/*-----------------------------------------------------------*/

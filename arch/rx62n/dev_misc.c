/*
   FreeRTOS V6.1.0 - Copyright (C) 2010 Real Time Engineers Ltd.
   This file is part of the FreeRTOS distribution.

   FreeRTOS is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License (version 2) as published by the
   Free Software Foundation AND MODIFIED BY the FreeRTOS exception.
 ***NOTE*** The exception to the GPL is included to allow you to distribute
 a combined work that includes FreeRTOS without being obliged to provide the
 source code for proprietary components outside of the FreeRTOS kernel.
 FreeRTOS is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 more details. You should have received a copy of the GNU General Public
 License and the FreeRTOS license exception along with FreeRTOS; if not it
 can be viewed here: http://www.freertos.org/a00114.html and also obtained
 by writing to Richard Barry, contact details for whom are available on the
 FreeRTOS WEB site.

 1 tab == 4 spaces!

http://www.FreeRTOS.org - Documentation, latest information, license and
contact details.

http://www.SafeRTOS.com - A version that is certified for use in safety
critical systems.

http://www.OpenRTOS.com - Commercial support, development, porting,
licensing and training services.
*/

/* ****************************************************************************
 * This project includes a lot of tasks and tests and is therefore complex.
 * If you would prefer a much simpler project to get started with then select
 * the 'Blinky' build configuration within the HEW IDE.
 * ****************************************************************************
 *
 * Creates all the demo application tasks, then starts the scheduler.  The web
 * documentation provides more details of the standard demo application tasks,
 * which provide no particular functionality but do provide a good example of
 * how to use the FreeRTOS API.  The tasks defined in flop.c are included in the
 * set of standard demo tasks to ensure the floating point unit gets some
 * exercise.
 *
 * In addition to the standard demo tasks, the following tasks and tests are
 * defined and/or created within this file:
 *
 * Webserver ("uIP") task - This serves a number of dynamically generated WEB
 * pages to a standard WEB browser.  The IP and MAC addresses are configured by
 * constants defined at the bottom of FreeRTOSConfig.h.  Use either a standard
 * Ethernet cable to connect through a hug, or a cross over (point to point)
 * cable to connect directly.  Ensure the IP address used is compatible with the
 * IP address of the machine running the browser - the easiest way to achieve
 * this is to ensure the first three octets of the IP addresses are the same.
 *
 * "Reg test" tasks - These fill the registers with known values, then check
 * that each register still contains its expected value.  Each task uses
 * different values.  The tasks run with very low priority so get preempted
 * very frequently.  A check variable is incremented on each iteration of the
 * test loop.  A register containing an unexpected value is indicative of an
 * error in the context switching mechanism and will result in a branch to a
 * null loop - which in turn will prevent the check variable from incrementing
 * any further and allow the check task (described below) to determine that an
 * error has occurred.  The nature of the reg test tasks necessitates that they
 * are written in assembly code.
 *
 * "Check" task - This only executes every five seconds but has a high priority
 * to ensure it gets processor time.  Its main function is to check that all the
 * standard demo tasks are still operational.  While no errors have been
 * discovered the check task will toggle LED 5 every 5 seconds - the toggle
 * rate increasing to 200ms being a visual indication that at least one task has
 * reported unexpected behaviour.
 *
 * "High frequency timer test" - A high frequency periodic interrupt is
 * generated using a timer - the interrupt is assigned a priority above
 * configMAX_SYSCALL_INTERRUPT_PRIORITY so should not be effected by anything
 * the kernel is doing.  The frequency and priority of the interrupt, in
 * combination with other standard tests executed in this demo, should result
 * in interrupts nesting at least 3 and probably 4 deep.  This test is only
 * included in build configurations that have the optimiser switched on.  In
 * optimised builds the count of high frequency ticks is used as the time base
 * for the run time stats.
 *
 * *NOTE 1* If LED5 is toggling every 5 seconds then all the demo application
 * tasks are executing as expected and no errors have been reported in any
 * tasks.  The toggle rate increasing to 200ms indicates that at least one task
 * has reported unexpected behaviour.
 *
 * *NOTE 2* vApplicationSetupTimerInterrupt() is called by the kernel to let
 * the application set up a timer to generate the tick interrupt.  In this
 * example a compare match timer is used for this purpose.
 *
 * *NOTE 3* The CPU must be in Supervisor mode when the scheduler is started.
 * The PowerON_Reset_PC() supplied in resetprg.c with this demo has
 * Change_PSW_PM_to_UserMode() commented out to ensure this is the case.
 *
 * *NOTE 4* The IntQueue common demo tasks test interrupt nesting and make use
 * of all the 8bit timers (as two cascaded 16bit units).
 */

/* Hardware specific includes. */
#include "iodefine.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

#include "lcd.h"
#include "i2c.h"
#include "temp_board.h"
#include "accelerometer.h"
#include "adc.h"

#include "stdio.h" 
#include "dev_misc.h"
#include "print.h"
#include "sci2.h"

#define temperature_TASK_PRIORITY	( tskIDLE_PRIORITY + 1)
#define grbl_TASK_PRIORITY		( tskIDLE_PRIORITY + 1)

/*
 * vApplicationMallocFailedHook() will only be called if
 * configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
 * function that will execute if a call to pvPortMalloc() fails.
 * pvPortMalloc() is called internally by the kernel whenever a task, queue or
 * semaphore is created.  It is also called by various parts of the demo
 * application.
 */
void vApplicationMallocFailedHook( void );

xTaskHandle grbl_handle;
extern int grbl_main();
extern void vuIP_Task(void*);
/*
 * vApplicationStackOverflowHook() will only be called if
 * configCHECK_FOR_STACK_OVERFLOW is set to a non-zero value.  The handle and
 * name of the offending task should be passed in the function parameters, but
 * it is possible that the stack overflow will have corrupted these - in which
 * case pxCurrentTCB can be inspected to find the same information.
 */
void vApplicationStackOverflowHook( xTaskHandle *pxTask, signed char *pcTaskName );


/*-----------------------------------------------------------*/

extern void HardwareSetup( void );

void temp_accel_task(void *pvParameters);
void grbl_task(void *pvParameters);

void debug(char * str){
	// De-Assert the CS for spi device - WIFI 
	PORTC.DR.BIT.B1 = 1 ;	
	PORTC.DR.BIT.B2 = 0	  ;

	lcd_string(LCD_LINE7,0 ,str);

	// Re-Assert the CS for spi device - WIFI
	PORTC.DR.BIT.B2 = 1	  ;
	PORTC.DR.BIT.B1 = 0 ;	 
}

void led_toggle()
{
	/*static int flag = 1;
	if(flag) {
		ALL_LEDS_ON
		flag = 0;
	} else {
		ALL_LEDS_OFF
		flag = 1;	
	}*/
}

extern void printString(const char * );

void dev_print_flash(const char *s)
{
	printString(s);
}

void dev_enable_ints()
{

}

void dev_disable_ints()
{

}

void delay_ms(double time_ms) 
{
	portTickType xLastWakeTime = xTaskGetTickCount();      // Initialise the xLastWakeTime variable with the current time.
	vTaskDelayUntil( &xLastWakeTime, time_ms/portTICK_RATE_MS); // Wait for the next cycle.
}

void delay_us(double time_us)
{

}

void sleep_mode()
{

}

void serial_poll_task(void *);

int main(void) {
	HardwareSetup();

	lcd_open();
	lcd_set_address(0, 0);
	lcd_string(LCD_LINE0,0, "   Welcome");

	i2c_init();
	temp_init(); //tempature sensor
	accel_init(); //accelerometer
	accel_calibrate_zero();
	//adc_init();

	sci2_init();

	printString("GRBL started\n");	

	// Application Tasks
	xTaskCreate(serial_poll_task, ( signed char * ) "serial", configMINIMAL_STACK_SIZE*1, NULL, grbl_TASK_PRIORITY, &grbl_handle);
	xTaskCreate(grbl_task, ( signed char * ) "grbl", configMINIMAL_STACK_SIZE*7, NULL, grbl_TASK_PRIORITY, &grbl_handle);
	xTaskCreate(temp_accel_task, ( signed char * ) "temp-accel", configMINIMAL_STACK_SIZE*2, NULL, temperature_TASK_PRIORITY, NULL );
	xTaskCreate(vuIP_Task, ( signed char * ) "uIP", configMINIMAL_STACK_SIZE*5, NULL, grbl_TASK_PRIORITY, NULL );
	
	/* Start the tasks running. */
	vTaskStartScheduler();

	debug("error????");
	for( ;; );

	return 0;
}

/*-----------------------------------------------------------*/

/* The RX port uses this callback function to configure its tick interrupt.
   This allows the application to choose the tick interrupt source. */
void vApplicationSetupTimerInterrupt( void )
{
	/* Enable compare match timer 0. */
	MSTP( CMT0 ) = 0;

	/* Interrupt on compare match. */
	CMT0.CMCR.BIT.CMIE = 1;

	/* Set the compare match value. */
	CMT0.CMCOR = ( unsigned short ) ( ( ( configPERIPHERAL_CLOCK_HZ / configTICK_RATE_HZ ) -1 ) / 8 );

	/* Divide the PCLK by 8. */
	CMT0.CMCR.BIT.CKS = 0;

	/* Enable the interrupt... */
	_IEN( _CMT0_CMI0 ) = 1;

	/* ...and set its priority to the application defined kernel priority. */
	_IPR( _CMT0_CMI0 ) = configKERNEL_INTERRUPT_PRIORITY;

	/* Start the timer. */
	CMT.CMSTR0.BIT.STR0 = 1;
}

void grbl_task(void *pvParameters)
{
	//vTaskSuspend( NULL ); // will be invoked via the shell	
	grbl_main();
}


/*-----------------------------------------------------------*/
/*-----------------------------------------------------------*/
void temp_accel_task(void *pvParameters)
{
	portTickType xLastWakeTime;

	const portTickType xFrequency = 1000/portTICK_RATE_MS; // update every 1 seconds

	char str[30];

	int16_t x = 0;
	int16_t y = 0;
	int16_t z = 0;

	float temp = 0.0f;

	xLastWakeTime = xTaskGetTickCount();      // Initialise the xLastWakeTime variable with the current time.
	
	for( ;; ){	

		x = accel_get_x();
		y = accel_get_y();
		z = accel_get_z();

		sprintf(str,"X = %i  ",(int)x);
		lcd_string(LCD_LINE2, 0, str);	
		sprintf(str,"Y = %i  ",(int)y);
		lcd_string(LCD_LINE3, 0, str);
		sprintf(str,"Z = %i  ",(int)z);
		lcd_string(LCD_LINE4, 0, str);


		temp = temp_read(); 
		sprintf(str,"temp = %f",(double)temp);
		lcd_string(LCD_LINE5, 0, str);

		vTaskDelayUntil( &xLastWakeTime, xFrequency ); // Wait for the next cycle.
	}
}

/* This function is explained by the comments above its prototype at the top
   of this file. */
void vApplicationMallocFailedHook( void )
{
	debug(" MallocFailed!");
	for( ;; );
}
/*-----------------------------------------------------------*/


/* This function is explained by the comments above its prototype at the top
   of this file. */
void vApplicationStackOverflowHook( xTaskHandle *pxTask, signed char *pcTaskName )
{
	debug("overflow");
	for( ;; );
}
/*-----------------------------------------------------------*/



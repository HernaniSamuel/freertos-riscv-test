#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* -----------------------------------------------------------------------
 * FreeRTOS configuration for a bare-metal RV32IM emulator.
 *
 * Memory map assumed:
 *   RAM:      0x00000000  (64 KB default)
 *   CLINT:    0x02000000
 *   UART:     0x10000000
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * Core scheduler settings
 * ----------------------------------------------------------------------- */

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ              ( ( unsigned long ) 10000000 ) // 10MHz fictício
#define configTICK_RATE_HZ              ( ( TickType_t ) 100 )
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 1024 * 1024 ) )
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_TRACE_FACILITY                0
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* -----------------------------------------------------------------------
 * CLINT timer registers (must match emulator memory map)
 * ----------------------------------------------------------------------- */

#define configMTIME_BASE_ADDRESS                ( 0x0200BFF8UL )
#define configMTIMECMP_BASE_ADDRESS             ( 0x02004000UL )

/* -----------------------------------------------------------------------
 * Memory allocation
 * ----------------------------------------------------------------------- */

#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

/* -----------------------------------------------------------------------
 * Software timers 
 * ----------------------------------------------------------------------- */

#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY      2
#define configTIMER_QUEUE_LENGTH       10
#define configTIMER_TASK_STACK_DEPTH   configMINIMAL_STACK_SIZE

/* -----------------------------------------------------------------------
 * Mutexes and semaphores
 * ----------------------------------------------------------------------- */

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               0

/* -----------------------------------------------------------------------
 * API inclusions
 * ----------------------------------------------------------------------- */

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

#endif /* FREERTOS_CONFIG_H */
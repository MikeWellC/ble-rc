#include "hal_types.h"
#include "OSAL.h"
#include "OSAL_Tasks.h"
#include "hal_drivers.h"
#include "ll.h"
#include "hci_tl.h"

#if defined (OSAL_CBTIMER_NUM_TASKS)
  #include "osal_cbTimer.h"
#endif

#include "l2cap.h"
#include "gap.h"
#include "gapgattserver.h"
#include "gapbondmgr.h"
#include "gatt.h"
#include "gattservapp.h"
#include "peripheral.h"
#include "racecar.h"

/**
 * The order in this table must be identical to the task initialization calls below in osalInitTask.
 */
const pTaskEventHandlerFn tasksArr[] =
{
	LL_ProcessEvent,                                                  // task 0
	Hal_ProcessEvent,                                                 // task 1
	HCI_ProcessEvent,                                                 // task 2
#if defined (OSAL_CBTIMER_NUM_TASKS)
	OSAL_CBTIMER_PROCESS_EVENT( osal_CbTimerProcessEvent ),           // task 3
#endif
	L2CAP_ProcessEvent,                                               // task 4
	GAP_ProcessEvent,                                                 // task 5
	GATT_ProcessEvent,                                                // task 6
	SM_ProcessEvent,                                                  // task 7
	GAPRole_ProcessEvent,                                             // task 8
	GAPBondMgr_ProcessEvent,                                          // task 9
	GATTServApp_ProcessEvent,                                   	  // task 10
	RaceCarProcessEvent                                  		      // task 11
};

const uint8 tasksCnt = sizeof( tasksArr ) / sizeof( tasksArr[0] );
uint16 *tasksEvents;

/**
 * This function invokes the initialization function for each task.
 */
void osalInitTasks(void)
{
	uint8 taskID = 0;

	tasksEvents = (uint16 *)osal_mem_alloc( sizeof( uint16 ) * tasksCnt);
	osal_memset( tasksEvents, 0, (sizeof( uint16 ) * tasksCnt));

	/* LL Task */
	LL_Init(taskID++);

	/* Hal Task */
	Hal_Init( taskID++ );

	/* HCI Task */
	HCI_Init(taskID++);

#if defined ( OSAL_CBTIMER_NUM_TASKS )
	/* Callback Timer Tasks */
	osal_CbTimerInit(taskID);
	taskID += OSAL_CBTIMER_NUM_TASKS;
#endif

	/* L2CAP Task */
	L2CAP_Init(taskID++);

	/* GAP Task */
	GAP_Init(taskID++);

	/* GATT Task */
	GATT_Init(taskID++);

	/* SM Task */
	SM_Init(taskID++);

	/* Profiles */
	GAPRole_Init(taskID++);
	GAPBondMgr_Init(taskID++);

	GATTServApp_Init(taskID++);

	/* Application */
	RaceCarInit(taskID);
};

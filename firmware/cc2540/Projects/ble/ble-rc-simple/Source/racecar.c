#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"
#include "OnBoard.h"
#include "gatt.h"
#include "hci.h"
#include "gapgattserver.h"
#include "gattservapp.h"
#include "devinfoservice-st.h"
#include "peripheral.h"
#include "gapbondmgr.h"
#include "racecar.h"
#include "battservice.h"
#include "blercprofile.h"
#include "car_control.h"
#include "osal_snv.h"

#if defined FEATURE_OAD
  #include "oad.h"
  #include "oad_target.h"
#endif

#define STATUS_UPDATE_EVENT_PERIOD		5000

// What is the advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL          160

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         FALSE

// General discoverable mode advertises indefinitely
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_GENERAL

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     800

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     8000

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          1000

static uint8 rcTaskId;   // Task ID for internal task/event processing
static gaprole_States_t gapProfileState = GAPROLE_INIT;

/**
 * GAP - SCAN RSP data (max size = 31 bytes)
 */
static uint8 scanResponseData[] =
{
	// complete name
	0x15,  // length of this data
	GAP_ADTYPE_LOCAL_NAME_COMPLETE,
	'B', 'L', 'E', '-', 'R', 'C', '-', 'S', 'I', 'M', 'P', 'L', 'E', 0, 0, 0, 0, 0, 0, 0, 0,
	// connection interval range
	0x05,   // length of this data
	GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
	LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),   // 100ms
	HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),
	LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),   // 1s
	HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),
	// Tx power level
	0x02,   // length of this data
	GAP_ADTYPE_POWER_LEVEL,
	0       // 0dBm
};

/**
 * GAP - Advertisement data (max size = 31 bytes, though this is
 * best kept short to conserve power while advertisting)
 */
static uint8 advertisementData[] =
{
	// Flags; this sets the device to use limited discoverable
	// mode (advertises for 30 seconds at a time) instead of general
	// discoverable mode (advertises indefinitely)
	0x02,   // length of this data
	GAP_ADTYPE_FLAGS,
	DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

	// service UUID, to notify central devices what services are included in this peripheral
	0x03,									// length of this data
	GAP_ADTYPE_16BIT_MORE,					// some of the UUID's, but not all
	LO_UINT16(BLE_RC_SERVICE_UUID),
	HI_UINT16(BLE_RC_SERVICE_UUID)
};

// GAP GATT Attributes
static uint8* attDeviceName = &scanResponseData[2];
static uint32 passCode = 0x00000000;

static void RcProcessOSALMsg(osal_event_hdr_t* msg);
static void RcPeripheralStateNotificationCB(gaprole_States_t newState);

/**
 * GAP Role Callbacks.
 */
static gapRolesCBs_t rcPeripheralCBs =
{
	RcPeripheralStateNotificationCB,	// Profile State Change Callbacks
	NULL								// When a valid RSSI is read from controller (not used by application)
};

/**
 * GAP Bond Manager Callbacks.
 */
static gapBondCBs_t rcBondMgrCBs =
{
	NULL,								// Passcode callback (not used by application)
	NULL								// Pairing / Bonding state Callback (not used by application)
};

/**
 * Initialization function for the BLE Peripheral App Task.
 * This is called during initialization and should contain any application specific initialization (ie. hardware initialization/setup, table initialization, power up notificaiton ... ).
 *
 * @param task_id - the ID assigned by OSAL. This ID should be used to send messages and set timers.
 */
void RaceCarInit(uint8 taskId)
{
	rcTaskId = taskId;

	if (SUCCESS == osal_snv_read(0x91, sizeof(uint8) * GAP_DEVICE_NAME_LEN, attDeviceName))
	{
	}
	else
	{
		attDeviceName[0] = 'F';
	}
	
	// Setup the GAP Peripheral Role Profile
	{
		uint8 initial_advertising_enable = TRUE;

		// By setting this to zero, the device will go into the waiting state after
		// being discoverable for 30.72 second, and will not being advertising again
		// until the enabler is set back to TRUE
		uint16 gapRole_AdvertOffTime = 0;

		uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
		uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
		uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
		uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
		uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

		// Set the GAP Role Parameters
		GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,		sizeof(uint8),				&initial_advertising_enable);
		GAPRole_SetParameter(GAPROLE_ADVERT_OFF_TIME,		sizeof(uint16),				&gapRole_AdvertOffTime);

		GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,			sizeof(scanResponseData),	scanResponseData);
		GAPRole_SetParameter(GAPROLE_ADVERT_DATA,			sizeof(advertisementData),	advertisementData);

		GAPRole_SetParameter(GAPROLE_PARAM_UPDATE_ENABLE,	sizeof(uint8),				&enable_update_request);
		GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL,		sizeof(uint16),				&desired_min_interval);
		GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL,		sizeof(uint16),				&desired_max_interval);
		GAPRole_SetParameter(GAPROLE_SLAVE_LATENCY,			sizeof(uint16),				&desired_slave_latency);
		GAPRole_SetParameter(GAPROLE_TIMEOUT_MULTIPLIER,	sizeof(uint16),				&desired_conn_timeout);
	}

	GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

	// Set advertising interval
	{
		uint16 advInt = DEFAULT_ADVERTISING_INTERVAL;

		GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MIN, advInt);
		GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MAX, advInt);
		GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MIN, advInt);
		GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MAX, advInt);
	}

	// Setup the GAP Bond Manager
	{
		uint8 pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
		uint8 mitm = TRUE;
		uint8 ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
		uint8 bonding = TRUE;

		GAPBondMgr_SetParameter(GAPBOND_DEFAULT_PASSCODE,	sizeof(uint32),	&passCode);
		GAPBondMgr_SetParameter(GAPBOND_PAIRING_MODE,		sizeof(uint8),	&pairMode);
		GAPBondMgr_SetParameter(GAPBOND_MITM_PROTECTION,	sizeof(uint8),	&mitm);
		GAPBondMgr_SetParameter(GAPBOND_IO_CAPABILITIES,	sizeof(uint8),	&ioCap);
		GAPBondMgr_SetParameter(GAPBOND_BONDING_ENABLED,	sizeof(uint8),	&bonding);
	}

	// Initialize GATT attributes
	GGS_AddService(GATT_ALL_SERVICES);            // GAP
	GATTServApp_AddService(GATT_ALL_SERVICES);    // GATT attributes
	DevInfo_AddService();                         // Device Information Service

	RcProfileAddService(GATT_ALL_SERVICES);

	#if defined FEATURE_OAD
		VOID OADTarget_AddService();                    // OAD Profile
	#endif

	Batt_AddService();

	RcInit(attDeviceName, &passCode);
	
	// Enable clock divide on halt.
	// This reduces active current while radio is active and CC254x MCU is halted.
	HCI_EXT_ClkDivOnHaltCmd(HCI_EXT_ENABLE_CLK_DIVIDE_ON_HALT);
	
	#if defined(DC_DC_P0_7)
		// Enable stack to toggle bypass control on TPS62730 (DC/DC converter)
		HCI_EXT_MapPmIoPortCmd( HCI_EXT_PM_IO_PORT_P0, HCI_EXT_PM_IO_PORT_PIN7 );
	#endif

	// Setup a delayed profile startup
	osal_set_event(rcTaskId, RC_START_DEVICE_EVT);
};

/**
 * BLE Peripheral Application Task event processor. This function is called to process all events for the task. Events include timers, messages and any other user defined events.
 *
 * @param task_id  - The OSAL assigned task ID.
 * @param events - events to process.  This is a bit map and can contain more than one event.
 * @return events not processed.
 */
uint16 RaceCarProcessEvent(uint8 taskId, uint16 events)
{
	VOID taskId; // OSAL required parameter that isn't used in this function

	if (events & SYS_EVENT_MSG)
	{
		uint8* msg = NULL;

		if ((msg = osal_msg_receive(rcTaskId)) != NULL)
		{
			RcProcessOSALMsg((osal_event_hdr_t*)msg);

			// Release the OSAL message
			VOID osal_msg_deallocate(msg);
		}

		// return unprocessed events
		return (events ^ SYS_EVENT_MSG);
	}

	if (events & RC_START_DEVICE_EVT)
	{
		// Start the Device
		VOID GAPRole_StartDevice(&rcPeripheralCBs);

		// Start Bond Manager
		VOID GAPBondMgr_Register(&rcBondMgrCBs);

		osal_start_timerEx(rcTaskId, RC_UPDATE_STATUS_EVT, 5000);
		
		return (events ^ RC_START_DEVICE_EVT);
	}

	if (events & RC_UPDATE_STATUS_EVT)
	{
		if (gapProfileState == GAPROLE_CONNECTED)
		{
			RcUpdateStatus();
			Batt_MeasLevel();
			
			// Restart timer
			osal_start_timerEx(rcTaskId, RC_UPDATE_STATUS_EVT, 5000);
		}
	
		return (events ^ RC_UPDATE_STATUS_EVT);
	}

	// Discard unknown events
	return 0;
};

/**
 * Process an incoming task message.
 *
 * @param pMsg - message to process.
 */
static void RcProcessOSALMsg(osal_event_hdr_t* msg)
{
	switch (msg->event)
	{
		default:
			break;
	}
};

/**
 * Notification from the profile of a state change.
 *
 * @param   newState - new state
 */
static void RcPeripheralStateNotificationCB(gaprole_States_t newState)
{
	switch (newState)
	{
		case GAPROLE_STARTED:
			{
				uint8 ownAddress[B_ADDR_LEN];
				uint8 systemId[DEVINFO_SYSTEM_ID_LEN];

				GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddress);

				// use 6 bytes of device address for 8 bytes of system ID value
				systemId[0] = ownAddress[0];
				systemId[1] = ownAddress[1];
				systemId[2] = ownAddress[2];

				// set middle bytes to zero
				systemId[4] = 0x00;
				systemId[3] = 0x00;

				// shift three bytes up
				systemId[7] = ownAddress[5];
				systemId[6] = ownAddress[4];
				systemId[5] = ownAddress[3];

				DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);
			}
			break;
		case GAPROLE_ADVERTISING:
			break;
		case GAPROLE_CONNECTED:
			RcStart(rcTaskId);
			osal_start_timerEx(rcTaskId, RC_UPDATE_STATUS_EVT, 5000);
			break;
		case GAPROLE_WAITING:
			RcStop();
			break;
		case GAPROLE_WAITING_AFTER_TIMEOUT:
			break;
		case GAPROLE_ERROR:
			break;
		default:
			break;
	}

	gapProfileState = newState;
};

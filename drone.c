/*
 * Copyright (c) 2015, Aurélien Zanelli <aurelien.zanelli@darkosphere.fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <libARDiscovery/ARDiscovery.h>
#include <libARCommands/ARCommands.h>

#include "drone.h"
#include "psplog.h"

#define DRONE_COMMAND_NO_ACK_ID 10
#define DRONE_COMMAND_ACK_ID 11
#define DRONE_COMMAND_EMERGENCY_ID 12
#define DRONE_EVENT_ID 126
#define DRONE_NAVDATA_ID 127

#define COMMAND_BUFFER_SIZE 512

/* client to device buffers definition */
static ARNETWORK_IOBufferParam_t c2d_buf_params[] = {
	/* non-acknowledged commands */
	{
		.ID = DRONE_COMMAND_NO_ACK_ID,
		.dataType = ARNETWORKAL_FRAME_TYPE_DATA,
		.sendingWaitTimeMs = 20,
		.ackTimeoutMs = ARNETWORK_IOBUFFERPARAM_INFINITE_NUMBER,
		.numberOfRetry = ARNETWORK_IOBUFFERPARAM_INFINITE_NUMBER,
		.numberOfCell = 2,
		.dataCopyMaxSize = 128,
		.isOverwriting = 1,
	},
	/* acknowledged commands */
	{
		.ID = DRONE_COMMAND_ACK_ID,
		.dataType = ARNETWORKAL_FRAME_TYPE_DATA_WITH_ACK,
		.sendingWaitTimeMs = 20,
		.ackTimeoutMs = 500,
		.numberOfRetry = 3,
		.numberOfCell = 20,
		.dataCopyMaxSize = 128,
		.isOverwriting = 0,
	},
	/* emergency commands */
	{
		.ID = DRONE_COMMAND_EMERGENCY_ID,
		.dataType = ARNETWORKAL_FRAME_TYPE_DATA_WITH_ACK,
		.sendingWaitTimeMs = 10,
		.ackTimeoutMs = 100,
		.numberOfRetry = ARNETWORK_IOBUFFERPARAM_INFINITE_NUMBER,
		.numberOfCell = 1,
		.dataCopyMaxSize = 128,
		.isOverwriting = 0,
	}
};
static const size_t n_c2d_buf_params = sizeof (c2d_buf_params) / sizeof (ARNETWORK_IOBufferParam_t);

/* device to client buffers definition */
static ARNETWORK_IOBufferParam_t d2c_buf_params[] = {
	/* navdata buffers */
	{
		.ID = DRONE_EVENT_ID,
		.dataType = ARNETWORKAL_FRAME_TYPE_DATA_WITH_ACK,
		.sendingWaitTimeMs = 20,
		.ackTimeoutMs = 500,
		.numberOfRetry = 3,
		.numberOfCell = 20,
		.dataCopyMaxSize = 128,
		.isOverwriting = 0,
	},
	/* event buffer */
	{
		.ID = DRONE_NAVDATA_ID,
		.dataType = ARNETWORKAL_FRAME_TYPE_DATA,
		.sendingWaitTimeMs = 20,
		.ackTimeoutMs = ARNETWORK_IOBUFFERPARAM_INFINITE_NUMBER,
		.numberOfRetry = ARNETWORK_IOBUFFERPARAM_INFINITE_NUMBER,
		.numberOfCell = 20,
		.dataCopyMaxSize = 128,
		.isOverwriting = 0,
	}
};
static const size_t n_d2c_buf_params = sizeof (d2c_buf_params) / sizeof (ARNETWORK_IOBufferParam_t);

static eARNETWORK_MANAGER_CALLBACK_RETURN
ar_network_command_cb (int buffer_id, uint8_t * data, void * userdata,
		eARNETWORK_MANAGER_CALLBACK_STATUS status)
{
	/*
	PSPLOG_DEBUG ("command callback with buffer id %d, status %d",
			buffer_id, status);
			*/

	if (status == ARNETWORK_MANAGER_CALLBACK_STATUS_TIMEOUT)
		return ARNETWORK_MANAGER_CALLBACK_RETURN_DATA_POP;

	return ARNETWORK_MANAGER_CALLBACK_RETURN_DEFAULT;
}

static void *
drone_navdata_buffer_thread (void * userdata)
{
	Drone *drone = (Drone *) userdata;
	uint8_t *buf;
	const size_t bufsize = 128 * 1024;

	/* allocate memory for incoming data */
	buf = malloc (bufsize);
	if (buf == NULL)
		return NULL;

	while (drone->running) {
		int skip = 0;
		int size;
		eARNETWORK_ERROR error = ARNETWORK_OK;

		error = ARNETWORK_Manager_ReadDataWithTimeout (drone->net,
				DRONE_NAVDATA_ID, buf, bufsize, &size, 1000);
		if (error != ARNETWORK_OK) {
			if (error != ARNETWORK_ERROR_BUFFER_EMPTY)
				PSPLOG_ERROR ("ARNETWORK_Manager_ReadDataWithTimeout failed, reason: %s",
						ARNETWORK_Error_ToString (error));

			skip = 1;
		}

		if (!skip) {
			eARCOMMANDS_DECODER_ERROR cmd_error;

			cmd_error = ARCOMMANDS_Decoder_DecodeBuffer (buf, size);
			if ((cmd_error != ARCOMMANDS_DECODER_OK) &&
					(cmd_error != ARCOMMANDS_DECODER_ERROR_NO_CALLBACK)) {
				char msg[128];
				ARCOMMANDS_Decoder_DescribeBuffer (buf, size, msg, sizeof(msg));
				PSPLOG_INFO ("ARCOMMANDS_Decoder_DecodeBuffer () failed : %d %s", cmd_error, msg);
			}
		}
	}

	free (buf);
	return NULL;
}

static void *
drone_event_buffer_thread (void * userdata)
{
	Drone *drone = (Drone *) userdata;
	uint8_t *buf;
	const size_t bufsize = 128 * 1024;

	/* allocate memory for incoming data */
	buf = malloc (bufsize);
	if (buf == NULL)
		return NULL;

	while (drone->running) {
		int skip = 0;
		int size;
		eARNETWORK_ERROR error = ARNETWORK_OK;

		error = ARNETWORK_Manager_ReadDataWithTimeout (drone->net,
				DRONE_EVENT_ID, buf, bufsize, &size, 1000);
		if (error != ARNETWORK_OK) {
			if (error != ARNETWORK_ERROR_BUFFER_EMPTY)
				PSPLOG_ERROR ("ARNETWORK_Manager_ReadDataWithTimeout failed, reason: %s",
						ARNETWORK_Error_ToString (error));

			skip = 1;
		}

		if (!skip) {
			eARCOMMANDS_DECODER_ERROR cmd_error;

			cmd_error = ARCOMMANDS_Decoder_DecodeBuffer (buf, size);
			if ((cmd_error != ARCOMMANDS_DECODER_OK) &&
					(cmd_error != ARCOMMANDS_DECODER_ERROR_NO_CALLBACK)) {
				char msg[128];
				ARCOMMANDS_Decoder_DescribeBuffer (buf, size, msg, sizeof(msg));
				PSPLOG_INFO ("ARCOMMANDS_Decoder_DecodeBuffer () failed : %d %s", cmd_error, msg);
			}
		}
	}

	free (buf);
	return NULL;
}

static eARDISCOVERY_ERROR
_on_send_json (uint8_t *data, uint32_t *size, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	if (data == NULL || size == NULL || userdata == NULL)
		return ARDISCOVERY_ERROR;

	PSPLOG_INFO ("on send json called");

	/* FIXME: does size contains the size of the input buffer ? to avoid
	 * possible buffer overflow */
	*size = sprintf((char *) data,
			"{ \"%s\": %d,\n \"%s\": \"%s\",\n \"%s\": \"%s\" }",
			ARDISCOVERY_CONNECTION_JSON_D2CPORT_KEY, drone->d2c_port,
			ARDISCOVERY_CONNECTION_JSON_CONTROLLER_NAME_KEY, "psp",
			ARDISCOVERY_CONNECTION_JSON_CONTROLLER_TYPE_KEY, "psp");
	*size++;

	return ARDISCOVERY_OK;
}

static eARDISCOVERY_ERROR
_on_receive_json (uint8_t *data, uint32_t size, char * ipv4, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	if (data == NULL || size == 0 || userdata == NULL)
		return ARDISCOVERY_ERROR;

	PSPLOG_INFO ("receive json from %s", ipv4);

	/* TODO: do something with json */
	return ARDISCOVERY_OK;
}

static void
on_battery_status_changed (uint8_t percent, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->battery = percent;
}

static void
on_flying_state_changed (eARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE state,
		void *userdata)
{
	Drone *drone = (Drone *) userdata;

	switch (state) {
		case ARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE_LANDED:
			drone->state = DRONE_STATE_LANDED;
			break;

		case ARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE_TAKINGOFF :
			drone->state = DRONE_STATE_TAKING_OFF;
			break;

		case ARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE_HOVERING:
		case ARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE_FLYING:
			drone->state = DRONE_STATE_FLYING;
			break;

		case ARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE_LANDING:
			drone->state = DRONE_STATE_LANDING;
			break;

		case ARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE_EMERGENCY:
			drone->state = DRONE_STATE_EMERGENCY;
			break;

		default:
			break;
	}
}

static void
on_hull_changed (uint8_t present, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->hull = present;
}

static void
on_altitude_changed (double altitude, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->altitude = (int) round(altitude);
}

static void
on_outdoor_flight_changed (uint8_t active, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->outdoor = active;
}

static void
on_autotakeoff_mode_changed (uint8_t active, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->autotakeoff = active;
}

static void
on_absolute_control_changed (uint8_t active, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->abs_control = active;
}

static void
on_gps_fixed_changed (uint8_t gps_fixed, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->gps_fixed = gps_fixed;
}

static void
on_position_changed (double latitude, double longitude, double altitude,
		void * userdata)
{
	Drone *drone = (Drone *) userdata;

	drone->gps_latitude = latitude;
	drone->gps_longitude = longitude;
	drone->gps_altitude = altitude;
}

static void
on_product_version_changed (char * software, char * hardware, void * userdata)
{
	Drone *drone = (Drone *) userdata;

	if (drone->software_version)
		free (drone->software_version);

	drone->software_version = strdup (software);

	if (drone->hardware_version)
		free (drone->hardware_version);

	drone->hardware_version = strdup (hardware);
}

static int
drone_discover (Drone * drone)
{
	eARDISCOVERY_ERROR err = ARDISCOVERY_OK;
	ARDISCOVERY_Connection_ConnectionData_t *discovery_data;

	PSPLOG_INFO ("creating discovery connection");

	discovery_data = ARDISCOVERY_Connection_New (_on_send_json,
			_on_receive_json, drone, &err);

	if (discovery_data == NULL || err != ARDISCOVERY_OK)
		goto new_discovery_failed;

	PSPLOG_INFO ("calling ARDISCOVERY_Connection_ControllerConnection");
	err = ARDISCOVERY_Connection_ControllerConnection (discovery_data,
			drone->discovery_port, drone->ipv4_addr);
	if (err != ARDISCOVERY_OK)
		goto not_connected;

	ARDISCOVERY_Connection_Delete (&discovery_data);
	return 0;

new_discovery_failed:
	PSPLOG_ERROR ("failed to create discovery, reason: %s",
			ARDISCOVERY_Error_ToString (err));
	if (discovery_data)
		ARDISCOVERY_Connection_Delete (&discovery_data);
	return -1;

not_connected:
	PSPLOG_ERROR ("failed to open discovery connection, reason: %s",
			ARDISCOVERY_Error_ToString (err));
	ARDISCOVERY_Connection_Delete (&discovery_data);
	return -1;
}

static void
_on_network_disconnected (ARNETWORK_Manager_t * net,
		ARNETWORKAL_Manager_t * net_al, void * userdata)
{
	PSPLOG_INFO ("on_network_disconnected called");
}

static int
drone_set_datetime (Drone * drone, time_t time)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;
	struct tm *tm;
	char tmp[255];

	tm = localtime (&time);
	if (tm == NULL) {
		PSPLOG_ERROR ("failed to get localtime");
		return -1;
	}

	if (strftime(tmp, sizeof(tmp), "%F", tm) == 0) {
		PSPLOG_ERROR ("failed to format time to string");
		return -1;
	}

	cmd_error = ARCOMMANDS_Generator_GenerateCommonCommonCurrentDate (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size, tmp);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate date command");
		return -1;
	}

	PSPLOG_DEBUG("send date command");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	if (strftime(tmp, sizeof(tmp), "%T%z", tm) == 0) {
		PSPLOG_ERROR ("failed to format time to string");
		return -1;
	}

	cmd_error = ARCOMMANDS_Generator_GenerateCommonCommonCurrentTime (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size, tmp);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate time command");
		return -1;
	}

	PSPLOG_DEBUG("send time command");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}


/* Drone API */
int
drone_init (Drone * drone)
{
	eARNETWORKAL_ERROR net_al_error = ARNETWORKAL_OK;
	memset (drone, 0, sizeof (*drone));

	drone->net_al = ARNETWORKAL_Manager_New (&net_al_error);
	if (net_al_error != ARNETWORKAL_OK) {
		PSPLOG_ERROR ("failed to create networl al manager");
		return -1;
	}

	/* general state callback */
	ARCOMMANDS_Decoder_SetCommonSettingsStateProductVersionChangedCallback (
			on_product_version_changed, drone);
	ARCOMMANDS_Decoder_SetCommonCommonStateBatteryStateChangedCallback (
			on_battery_status_changed, drone);

	/* piloting */
	ARCOMMANDS_Decoder_SetARDrone3PilotingStateFlyingStateChangedCallback (
			on_flying_state_changed, drone);
	ARCOMMANDS_Decoder_SetARDrone3PilotingStateAltitudeChangedCallback (
			on_altitude_changed, drone);
	ARCOMMANDS_Decoder_SetARDrone3PilotingStatePositionChangedCallback (
			on_position_changed, drone);

	/* piloting settings */
	ARCOMMANDS_Decoder_SetARDrone3SpeedSettingsStateHullProtectionChangedCallback (
			on_hull_changed, drone);
	ARCOMMANDS_Decoder_SetARDrone3SpeedSettingsStateOutdoorChangedCallback (
			on_outdoor_flight_changed, drone);
	ARCOMMANDS_Decoder_SetARDrone3PilotingStateAutoTakeOffModeChangedCallback (
			on_autotakeoff_mode_changed, drone);
	ARCOMMANDS_Decoder_SetARDrone3PilotingSettingsStateAbsolutControlChangedCallback (
			on_absolute_control_changed, drone);

	/* GPS callback */
	ARCOMMANDS_Decoder_SetARDrone3GPSSettingsStateGPSFixStateChangedCallback (
			on_gps_fixed_changed, drone);

	return 0;
}

void
drone_deinit (Drone * drone)
{
	PSPLOG_INFO ("deinitializing drone");

	if (drone->net_al) {
		PSPLOG_DEBUG ("deleting network al");
		ARNETWORKAL_Manager_Delete (&drone->net_al);
		drone->net_al = NULL;
	}

	if (drone->software_version)
		free (drone->software_version);

	if (drone->hardware_version)
		free (drone->hardware_version);
}

int
drone_connect (Drone * drone, const char * ipv4, int discovery_port,
		int c2d_port, int d2c_port)
{
	int ret;
	eARNETWORKAL_ERROR al_error;
	eARNETWORK_ERROR error;

	drone->ipv4_addr = strdup (ipv4);
	drone->discovery_port = discovery_port;
	drone->c2d_port = c2d_port;
	drone->d2c_port = d2c_port;

	PSPLOG_INFO ("connecting to drone %s", drone->ipv4_addr);

	if (drone_discover(drone) < 0)
		goto no_drone;

	al_error = ARNETWORKAL_Manager_InitWifiNetwork (drone->net_al,
			drone->ipv4_addr, drone->c2d_port, drone->d2c_port,
			5);
	if (al_error != ARNETWORKAL_OK)
		goto no_wlan;

	PSPLOG_DEBUG ("creating arnetwork manager");

	drone->net = ARNETWORK_Manager_New (drone->net_al, n_c2d_buf_params,
			c2d_buf_params, n_d2c_buf_params, d2c_buf_params, 0,
			_on_network_disconnected, drone, &error);
	if (drone->net == NULL || error != ARNETWORK_OK)
		goto no_net;

	/* create and start tx and rx thread */
	PSPLOG_DEBUG ("creating arnetwork rx thread");
	ret = ARSAL_Thread_Create(&drone->rx_thread,
			ARNETWORK_Manager_ReceivingThreadRun, drone->net);
	if (ret < 0)
		goto create_thread_failed;

	PSPLOG_DEBUG ("creating arnetowk tx thread");
	ret = ARSAL_Thread_Create(&drone->tx_thread,
			ARNETWORK_Manager_SendingThreadRun, drone->net);
	if (ret < 0)
		goto create_thread_failed;

	/* create and start event and navdata threads */
	drone->running = 1;
	PSPLOG_DEBUG ("creating event thread");
	ret = ARSAL_Thread_Create (&drone->event_thread,
			drone_event_buffer_thread, drone);
	if (ret < 0)
		goto create_thread_failed;

	PSPLOG_DEBUG ("creating navdata thread");
	ret = ARSAL_Thread_Create (&drone->navdata_thread,
			drone_navdata_buffer_thread, drone);
	if (ret < 0)
		goto create_thread_failed;

	PSPLOG_INFO ("connected to drone %s", drone->ipv4_addr);

	drone_set_datetime (drone, time (NULL));

	return 0;

no_drone:
	PSPLOG_ERROR ("failed to discover a drone");
	return -1;

no_wlan:
	PSPLOG_ERROR ("failed to initialize wifi network, reason: %s",
			ARNETWORKAL_Error_ToString (al_error));
	return -1;

no_net:
	PSPLOG_ERROR ("failed to initialize network manager, reason: %s",
			ARNETWORK_Error_ToString (error));
	ARNETWORKAL_Manager_CloseWifiNetwork (drone->net_al);
	return -1;

create_thread_failed:
	PSPLOG_ERROR ("failed to create a network or event thread");
	if (drone->rx_thread) {
		ARSAL_Thread_Join (drone->rx_thread, NULL);
		ARSAL_Thread_Destroy (&drone->rx_thread);
		drone->rx_thread = NULL;
	}

	if (drone->tx_thread) {
		ARSAL_Thread_Join (drone->tx_thread, NULL);
		ARSAL_Thread_Destroy (&drone->tx_thread);
		drone->tx_thread = NULL;
	}

	drone->running = 0;

	if (drone->event_thread) {
		ARSAL_Thread_Join (drone->event_thread, NULL);
		ARSAL_Thread_Destroy (&drone->event_thread);
		drone->event_thread = NULL;
	}

	if (drone->navdata_thread) {
		ARSAL_Thread_Join (drone->navdata_thread, NULL);
		ARSAL_Thread_Destroy (&drone->navdata_thread);
		drone->navdata_thread = NULL;
	}

	ARNETWORKAL_Manager_CloseWifiNetwork (drone->net_al);
	return -1;
}

int
drone_disconnect (Drone * drone)
{
	PSPLOG_INFO ("disconnecting from drone %s", drone->ipv4_addr);
	drone->running = 0;

	if (drone->event_thread) {
		PSPLOG_DEBUG ("stopping event thread");
		ARSAL_Thread_Join (drone->event_thread, NULL);
		ARSAL_Thread_Destroy (&drone->event_thread);
		drone->event_thread = NULL;
	}

	if (drone->navdata_thread) {
		PSPLOG_DEBUG ("stopping navdata thread");
		ARSAL_Thread_Join (drone->navdata_thread, NULL);
		ARSAL_Thread_Destroy (&drone->navdata_thread);
		drone->navdata_thread = NULL;
	}

	if (drone->net) {
		ARNETWORK_Manager_Stop (drone->net);

		if (drone->rx_thread) {
			PSPLOG_DEBUG ("joining with rx thread");
			ARSAL_Thread_Join (drone->rx_thread, NULL);
			ARSAL_Thread_Destroy (&drone->rx_thread);
			drone->rx_thread = NULL;
		}

		if (drone->tx_thread) {
			PSPLOG_DEBUG ("joining with tx thread");
			ARSAL_Thread_Join (drone->tx_thread, NULL);
			ARSAL_Thread_Destroy (&drone->tx_thread);
			drone->tx_thread = NULL;
		}

		PSPLOG_DEBUG ("deleting network manager");
		ARNETWORK_Manager_Delete (&drone->net);
		drone->net = NULL;
	}

	if (drone->net_al) {
		PSPLOG_DEBUG ("unlocking and closing network al manager");
		ARNETWORKAL_Manager_Unlock (drone->net_al);
		ARNETWORKAL_Manager_CloseWifiNetwork (drone->net_al);
	}

	if (drone->ipv4_addr)
		free (drone->ipv4_addr);

	drone->ipv4_addr = NULL;

	return 0;
}

int
drone_sync_state (Drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateCommonCommonAllStates (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate sync state");
		return -1;
	}

	PSPLOG_DEBUG ("send sync state");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_sync_settings (Drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR err;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	err = ARCOMMANDS_Generator_GenerateCommonSettingsAllSettings (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (err != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate sync settings");
		return -1;
	}

	PSPLOG_DEBUG ("send sync settings");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_flat_trim (Drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingFlatTrim (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate emergency command");
		return -1;
	}

	PSPLOG_DEBUG ("send flat trim");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_emergency (Drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingEmergency (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate emergency command");
		return -1;
	}

	PSPLOG_DEBUG ("send emergency");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_EMERGENCY_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_takeoff (Drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingTakeOff (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate takeoff command");
		return -1;
	}

	PSPLOG_DEBUG ("send takeoff");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_landing (Drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingLanding (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate landing command");
		return -1;
	}

	PSPLOG_DEBUG ("send landing");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_flight_control (Drone * drone, int gaz, int yaw, int pitch, int roll)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingPCMD (cmd,
			COMMAND_BUFFER_SIZE, &cmd_size, 1, roll, pitch, yaw,
			gaz, 0);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate flight control command");
		return -1;
	}

	PSPLOG_DEBUG ("send flight control parameters");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_NO_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_hull_set_active (Drone * drone, int active)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3SpeedSettingsHullProtection (
			cmd, COMMAND_BUFFER_SIZE, &cmd_size, active);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate flight control command");
		return -1;
	}

	PSPLOG_DEBUG ("send hull presence");
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_outdoor_flight_set_active (Drone * drone, int active)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3SpeedSettingsOutdoor (
			cmd, COMMAND_BUFFER_SIZE, &cmd_size, active);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate outdoor command");
		return -1;
	}

	PSPLOG_DEBUG ("send outdoor presence: %d", active);
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_autotakeoff_set_active (Drone * drone, int active)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3SpeedSettingsOutdoor (
			cmd, COMMAND_BUFFER_SIZE, &cmd_size, active);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate autotakeoff command");
		return -1;
	}

	PSPLOG_DEBUG ("send autotakeoff presence: %d", active);
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_absolute_control_set_active (Drone * drone, int active)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingSettingsAbsolutControl (
			cmd, COMMAND_BUFFER_SIZE, &cmd_size, active);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate absolute control command");
		return -1;
	}

	PSPLOG_DEBUG ("send absolute control: %d", active);
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_do_flip (Drone * drone, DroneFlip flip)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;
	eARCOMMANDS_ARDRONE3_ANIMATIONS_FLIP_DIRECTION dir;

	switch (flip) {
		case DRONE_FLIP_FRONT:
			dir = ARCOMMANDS_ARDRONE3_ANIMATIONS_FLIP_DIRECTION_FRONT;
			break;
		case DRONE_FLIP_BACK:
			dir = ARCOMMANDS_ARDRONE3_ANIMATIONS_FLIP_DIRECTION_BACK;
			break;
		case DRONE_FLIP_LEFT:
			dir = ARCOMMANDS_ARDRONE3_ANIMATIONS_FLIP_DIRECTION_LEFT;
			break;
		case DRONE_FLIP_RIGHT:
			dir = ARCOMMANDS_ARDRONE3_ANIMATIONS_FLIP_DIRECTION_RIGHT;
			break;
		default:
			return -1;
			break;
	}

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3AnimationsFlip (
			cmd, COMMAND_BUFFER_SIZE, &cmd_size, dir);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR ("failed to generate flip command");
		return -1;
	}

	PSPLOG_DEBUG ("send flip: %d", flip);
	ARNETWORK_Manager_SendData (drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

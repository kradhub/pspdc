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
	struct drone *drone = (struct drone *) userdata;
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
	struct drone *drone = (struct drone *) userdata;
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
	struct drone *drone = (struct drone *) userdata;

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
	struct drone *drone = (struct drone *) userdata;

	if (data == NULL || size == 0 || userdata == NULL)
		return ARDISCOVERY_ERROR;

	PSPLOG_INFO ("receive json from %s", ipv4);

	/* TODO: do something with json */
	return ARDISCOVERY_OK;
}


static int
drone_discover (struct drone * drone)
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
drone_connect (struct drone * drone)
{
	int ret;
	eARNETWORKAL_ERROR al_error;
	eARNETWORK_ERROR error;

	PSPLOG_INFO ("connecting to drone %s", drone->ipv4_addr);

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

	PSPLOG_INFO ("connected to drone %s", drone->ipv4_addr);

	return 0;

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
	PSPLOG_ERROR ("failed to create a network thread");
	if (drone->rx_thread) {
		ARSAL_Thread_Join (drone->rx_thread, NULL);
		ARSAL_Thread_Destroy (&drone->rx_thread);
		drone->rx_thread = NULL;
	}

	ARNETWORKAL_Manager_CloseWifiNetwork (drone->net_al);
	return -1;
}

static int
drone_disconnect (struct drone * drone)
{
	PSPLOG_INFO ("disconnecting from drone %s", drone->ipv4_addr);

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

	return 0;
}


/* Drone API */
int
drone_init(struct drone * drone, char * ipv4, int discovery_port,
		int c2d_port, int d2c_port)
{
	int ret;
	eARNETWORKAL_ERROR net_al_error = ARNETWORKAL_OK;

	PSPLOG_INFO ("Initializing drone");

	/* set some device params */
	memset (drone, 0, sizeof (*drone));
	drone->ipv4_addr = strdup (ipv4);
	drone->discovery_port = discovery_port;
	drone->d2c_port = d2c_port;
	drone->c2d_port = c2d_port;
	drone->running = 1;

	drone->net_al = ARNETWORKAL_Manager_New (&net_al_error);
	if (net_al_error != ARNETWORKAL_OK) {
		PSPLOG_ERROR ("failed to create networl al manager");
		goto cleanup;
	}

	ret = drone_discover (drone);
	if (ret < 0) {
		PSPLOG_ERROR ("failed to discover");
		goto cleanup;
	}

	ret = drone_connect (drone);
	if (ret < 0) {
		PSPLOG_ERROR ("failed to connect to drone");
		goto cleanup;
	}

	/* allocate reader thread array and data array */
	ret = ARSAL_Thread_Create (&drone->event_thread,
			drone_event_buffer_thread, drone);
	if (ret < 0) {
		PSPLOG_ERROR ("failed to create event thread");
		goto cleanup;
	}

	ret = ARSAL_Thread_Create (&drone->navdata_thread,
			drone_navdata_buffer_thread, drone);
	if (ret < 0) {
		PSPLOG_ERROR ("failed to create navdata thread");
		goto cleanup;
	}

	PSPLOG_INFO ("drone initialized");

	return 0;

cleanup:
	drone->running = 0;

	if (drone->net_al)
		ARNETWORKAL_Manager_Delete (&drone->net_al);

	drone_disconnect (drone);

	if (drone->event_thread) {
		ARSAL_Thread_Join (drone->event_thread, NULL);
		ARSAL_Thread_Destroy (&drone->event_thread);
	}

	if (drone->navdata_thread) {
		ARSAL_Thread_Join (drone->navdata_thread, NULL);
		ARSAL_Thread_Destroy (&drone->navdata_thread);
	}

	drone->net_al = NULL;
	drone->event_thread = NULL;
	drone->navdata_thread = NULL;

	return -1;
}

void
drone_deinit (struct drone * drone)
{
	PSPLOG_INFO ("deinitializing drone");
	drone->running = 0;

	drone_disconnect (drone);

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

	if (drone->net_al) {
		PSPLOG_DEBUG ("deleting network al");
		ARNETWORKAL_Manager_Delete (&drone->net_al);
		drone->net_al = NULL;
	}

	if (drone->ipv4_addr)
		free (drone->ipv4_addr);
}

int drone_flat_trim (struct drone * drone)
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

	PSPLOG_INFO ("send flat trim");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int drone_emergency(struct drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingEmergency(cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR("failed to generate emergency command");
		return -1;
	}

	PSPLOG_INFO("send emergency");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_EMERGENCY_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_takeoff(struct drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingTakeOff(cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR("failed to generate takeoff command");
		return -1;
	}

	PSPLOG_INFO("send takeoff");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_landing (struct drone * drone)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingLanding(cmd,
			COMMAND_BUFFER_SIZE, &cmd_size);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR("failed to generate landing command");
		return -1;
	}

	PSPLOG_INFO("send landing");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

int
drone_flight_control (struct drone * drone, int gaz, int yaw, int pitch,
		int roll)
{
	eARCOMMANDS_GENERATOR_ERROR cmd_error;
	uint8_t cmd[COMMAND_BUFFER_SIZE];
	int32_t cmd_size;

	cmd_error = ARCOMMANDS_Generator_GenerateARDrone3PilotingPCMD(cmd,
			COMMAND_BUFFER_SIZE, &cmd_size, 1, roll, pitch, yaw,
			gaz, 0);
	if (cmd_error != ARCOMMANDS_GENERATOR_OK) {
		PSPLOG_ERROR("failed to generate flight control command");
		return -1;
	}

	PSPLOG_INFO("send flight control parameters");
	ARNETWORK_Manager_SendData(drone->net, DRONE_COMMAND_NO_ACK_ID,
			cmd, cmd_size, NULL, &ar_network_command_cb, 1);

	return 0;
}

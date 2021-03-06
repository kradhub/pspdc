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

#ifndef DRONE_H
#define DRONE_H

#include <libARSAL/ARSAL.h>
#include <libARNetworkAL/ARNetworkAL.h>
#include <libARNetwork/ARNetwork.h>

typedef enum
{
	DRONE_STATE_LANDED = 0,
	DRONE_STATE_TAKING_OFF,
	DRONE_STATE_FLYING,
	DRONE_STATE_LANDING,
	DRONE_STATE_EMERGENCY
} DroneState;

typedef enum
{
	DRONE_FLIP_FRONT = 0,
	DRONE_FLIP_BACK,
	DRONE_FLIP_RIGHT,
	DRONE_FLIP_LEFT
} DroneFlip;

typedef struct _drone Drone;
typedef struct _drone_setting DroneSetting;

struct _drone_setting
{
	int min;
	int max;
	int current;
};

struct _drone
{
	char *ipv4_addr;
	int discovery_port;
	int d2c_port;
	int c2d_port;
	int connected;

	ARNETWORKAL_Manager_t *net_al;
	ARNETWORK_Manager_t *net;
	ARSAL_Thread_t rx_thread;
	ARSAL_Thread_t tx_thread;

	/* drone buffer rx threads */
	ARSAL_Thread_t event_thread;
	ARSAL_Thread_t navdata_thread;

	int running;
	int state_sync;
	int settings_sync;

	/* drone state */
	DroneState state;
	unsigned int battery;
	unsigned int hull;
	int altitude;
	unsigned int outdoor;
	unsigned int gps_fixed;
	double gps_latitude;
	double gps_longitude;
	double gps_altitude;
	char *software_version;
	char *hardware_version;
	char *arcommand_version;

	DroneSetting altitude_limit;
	DroneSetting vertical_speed_limit;
	DroneSetting rotation_speed_limit;
	DroneSetting tilt_limit;;
};

int drone_init (Drone * drone);
void drone_deinit (Drone * drone);

int drone_connect (Drone * drone, const char * ipv4, int discovery_port,
		int c2d_port, int d2c_port);
int drone_disconnect (Drone * drone);

int drone_emergency (Drone * drone);
int drone_takeoff (Drone * drone);
int drone_landing (Drone * drone);

int drone_flat_trim (Drone * drone);

int drone_sync_state (Drone * drone);

/* piloting commands */
int drone_flight_control (Drone * drone, int gaz, int yaw, int pitch, int roll);
int drone_do_flip (Drone * drone, DroneFlip flip);

/* settings commands */
int drone_sync_settings (Drone * drone);
int drone_hull_set_active (Drone * drone, int active);
int drone_outdoor_flight_set_active (Drone * drone, int active);
int drone_altitude_limit_set (Drone * drone, int limit);
int drone_vertical_speed_limit_set (Drone * drone, int limit);
int drone_rotation_speed_limit_set (Drone * drone, int limit);
int drone_max_tilt_set (Drone * drone, int limit);
int drone_streaming_set_active (Drone * drone, int active);

int drone_take_picture (Drone * drone);

#endif

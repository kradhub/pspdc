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

struct drone
{
	char *ipv4_addr;
	int discovery_port;
	int d2c_port;
	int c2d_port;

	ARNETWORKAL_Manager_t *net_al;
	ARNETWORK_Manager_t *net;
	ARSAL_Thread_t rx_thread;
	ARSAL_Thread_t tx_thread;

	/* drone buffer rx threads */
	ARSAL_Thread_t event_thread;
	ARSAL_Thread_t navdata_thread;

	int running;
};

int drone_init (struct drone * drone, char * ipv4, int discovery_port,
		int c2d_port, int d2c_port);
void drone_deinit (struct drone * drone);

int drone_emergency (struct drone * drone);
int drone_takeoff (struct drone * drone);
int drone_landing (struct drone * drone);

/* piloting commands */
int drone_flight_control (struct drone * drone, int gaz, int yaw, int pitch,
		int roll);

#endif

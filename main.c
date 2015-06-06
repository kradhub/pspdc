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

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <psputility_netmodules.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "psplog.h"
#include "drone.h"
#include "ui.h"

#define DRONE_IP "192.168.42.1"
#define DRONE_DISCOVERY_PORT 44444
#define DRONE_C2D_PORT 54321
#define DRONE_D2C_PORT 43210

PSP_MODULE_INFO("PSP Drone Control", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_MAX();

int running = 1;

static int on_app_exit(int arg1, int arg2, void *common)
{
	running = 0;
	return 0;
}

int callback_thread(SceSize args, void *argp)
{
	int callback_id;

	callback_id = sceKernelCreateCallback("Exit Callback", on_app_exit,
			NULL);
	sceKernelRegisterExitCallback(callback_id);

	sceKernelSleepThreadCB();

	return 0;
}

int setup_callback()
{
	int thread_id;

	thread_id = sceKernelCreateThread("Callback update thread",
			callback_thread, 0x11, 0xFA0, THREAD_ATTR_USER, 0);

	if (thread_id >= 0)
		sceKernelStartThread(thread_id, 0, 0);

	return thread_id;
}


static int network_init()
{
	if (sceNetInit(128 * 4096, 42, 4096, 42, 4096) < 0) {
		PSPLOG_ERROR("failed to initialize net component");
		return -1;
	}

	if (sceNetInetInit() < 0)
		goto inet_init_failed;

	if (sceNetApctlInit(0x8000, 48) < 0)
		goto apctl_init_failed;

	return 0;

inet_init_failed:
	PSPLOG_ERROR("failed to initialize inet");
	sceNetTerm();
	return -1;

apctl_init_failed:
	PSPLOG_ERROR("failed to initialize apctl");
	sceNetInetTerm();
	sceNetTerm();
	return -1;
}

static void network_deinit()
{
	sceNetApctlTerm();
	sceNetInetTerm();
	sceNetTerm();
}

int init_subsystem()
{
	PSPLOG_DEBUG("loading net module");
	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);

	PSPLOG_DEBUG("initializing network stack");
	if (network_init() < 0)
		goto network_init_failed;

	PSPLOG_DEBUG("initializing SDL");
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		goto sdl_init_failed;

	PSPLOG_DEBUG("initializing SDL_ttf");
	if (TTF_Init() < 0)
		goto ttf_init_failed;

	return 0;

network_init_failed:
	PSPLOG_ERROR("failed to initialize network");
	return -1;

sdl_init_failed:
	PSPLOG_ERROR("failed to initialize SDL");
	network_deinit();
	return -1;

ttf_init_failed:
	PSPLOG_ERROR("failed to initialize SDL_ttf");
	SDL_Quit();
	network_deinit();
	return -1;
}

void deinit_subsystem()
{
	TTF_Quit();
	SDL_Quit();
	network_deinit();
}

int main(int argc, char *argv[])
{
	Drone drone;
	int ret;
	union SceNetApctlInfo ssid;
	union SceNetApctlInfo gateway;
	union SceNetApctlInfo ip;
	UI ui;

	setup_callback();

	if (psplog_init(PSPLOG_CAT_INFO, "ms0:/PSP/GAME/pspdc/log") < 0)
		sceKernelExitGame();

	if (init_subsystem() < 0)
		goto end;

	if (ui_init(&ui, 480, 272) < 0)
		goto end;

main_menu:
	switch (ui_main_menu_run(&ui)) {
		case MAIN_MENU_CONNECT:
			break;
		case MAIN_MENU_EXIT:
		default:
			goto end;
	}

	PSPLOG_DEBUG("Opening network connection dialog");
	if (ui_network_dialog_run(&ui))
		goto main_menu;

	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SSID, &ssid);
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_GATEWAY, &gateway);
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &ip);

	PSPLOG_INFO("connected to %s (%s)", ssid.ssid, gateway.gateway);
	PSPLOG_INFO("get ip: %s", ip.ip);

	/* initialize and connect to drone */
	PSPLOG_INFO ("initializing drone");
	ret = drone_init (&drone, DRONE_IP, DRONE_DISCOVERY_PORT,
			DRONE_C2D_PORT, DRONE_D2C_PORT);
	if (ret < 0) {
		PSPLOG_ERROR ("failed to initialize drone");
	}

	ui_flight_run(&ui, &drone);

	drone_deinit (&drone);

end:
	ui_deinit(&ui);
	deinit_subsystem();
	psplog_deinit();
	sceKernelExitGame();
	return 0;
}

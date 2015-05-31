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
#include <psputility_netconf.h>
#include <pspgu.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pspctrl.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "psplog.h"
#include "drone.h"
#include "menu.h"
#include "color.h"

#define DRONE_IP "192.168.42.1"
#define DRONE_DISCOVERY_PORT 44444
#define DRONE_C2D_PORT 54321
#define DRONE_D2C_PORT 43210

PSP_MODULE_INFO("PSP Drone Control", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_MAX();

enum
{
	MAIN_MENU_CONNECT = 0,
	MAIN_MENU_EXIT
};

static int running = 1;

unsigned int __attribute__((aligned(16))) list[128];

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

static int network_dialog()
{
	pspUtilityNetconfData conf;
	struct pspUtilityNetconfAdhoc adhoc_params;

	memset(&conf, 0, sizeof(conf));
	memset(&adhoc_params, 0, sizeof(adhoc_params));

	conf.base.size = sizeof(conf);
	conf.base.language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	conf.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;

	/* Thread priorities */
	conf.base.graphicsThread = 17;
	conf.base.accessThread = 19;
	conf.base.fontThread = 18;
	conf.base.soundThread = 16;

	conf.action = PSP_NETCONF_ACTION_CONNECTAP;
	conf.adhocparam = &adhoc_params;

	sceUtilityNetconfInitStart (&conf);

	while (running) {
		int done = 0;

		/* directly use GU to avoid flickering with SDL */
		sceGuStart(GU_DIRECT, list);
		sceGuClearColor(0xff554433);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
		sceGuFinish();
		sceGuSync(0,0);

		switch (sceUtilityNetconfGetStatus()) {
			case PSP_UTILITY_DIALOG_NONE:
				break;

			case PSP_UTILITY_DIALOG_VISIBLE:
				sceUtilityNetconfUpdate(1);
				break;

			case PSP_UTILITY_DIALOG_QUIT:
				sceUtilityNetconfShutdownStart();
				break;

			case PSP_UTILITY_DIALOG_FINISHED:
				done = 1;
				break;

			default:
				break;
		}

		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();

		if (done)
			break;
	}

	return 0;
}

static int
main_menu_run(SDL_Surface * screen, TTF_Font *font)
{
	Menu *main_menu;
	SDL_Surface *surface;
	SDL_Rect position;
	int selected_id = -1;

	main_menu = menu_new(font, "Main menu");
	menu_add_entry(main_menu, MAIN_MENU_CONNECT, "Connect to drone");
	menu_add_entry(main_menu, MAIN_MENU_EXIT, "Exit");

	/* center position in screen */
	position.x = (screen->w - menu_get_width(main_menu)) / 2;
	position.y = (screen->h - menu_get_height(main_menu)) / 2;

	while (running) {
		SceCtrlLatch latch;

		surface = menu_render(main_menu);
		SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
		SDL_BlitSurface(surface, NULL, screen, &position);
		SDL_FreeSurface(surface);

		sceDisplayWaitVblankStart();
		SDL_Flip(screen);

		sceCtrlReadLatch(&latch);
		if ((latch.uiPress & PSP_CTRL_UP) &&
				(latch.uiMake & PSP_CTRL_UP))
			menu_select_prev_entry(main_menu);

		if ((latch.uiPress & PSP_CTRL_DOWN) &&
				(latch.uiMake & PSP_CTRL_DOWN))
			menu_select_next_entry(main_menu);

		if ((latch.uiPress & PSP_CTRL_CROSS) &&
				(latch.uiMake & PSP_CTRL_CROSS)) {
			selected_id = menu_get_selected_id(main_menu);
			break;
		}
	}

	menu_free(main_menu);
	return selected_id;
}

int main(int argc, char *argv[])
{
	struct drone drone;
	int ret;
	union SceNetApctlInfo ssid;
	union SceNetApctlInfo gateway;
	union SceNetApctlInfo ip;
	int is_flying = 0;
	SDL_Surface *screen;

	if (psplog_init("ms0:/PSP/GAME/pspdc/log") < 0)
		sceKernelExitGame();

	PSPLOG_INFO("PSP Drone Control");
	PSPLOG_DEBUG("Initializing components");

	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);

	if (network_init() < 0)
		sceKernelExitGame();

	setup_callback();

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		PSPLOG_ERROR("failed to initialize SDL");
		goto done;
	}

	if (TTF_Init() < 0) {
		PSPLOG_ERROR("failed to initialize TTF");
		SDL_Quit();
		goto done;
	}

	screen = SDL_SetVideoMode(480, 272, 32, SDL_HWSURFACE | SDL_DOUBLEBUF);
	if (screen == NULL) {
		PSPLOG_ERROR("failed to set screen video mode");
		TTF_Quit();
		SDL_Quit();
		goto done;
	}

	SDL_ShowCursor(SDL_DISABLE);

	TTF_Font *font;
	font = TTF_OpenFont("DejaVuSans.ttf", 16);
	if (font == NULL) {
		PSPLOG_ERROR("failed to log font");
		goto done;
	}

	int selected_id;
	selected_id = main_menu_run(screen, font);
	switch (selected_id) {
		case MAIN_MENU_CONNECT:
			break;
		case MAIN_MENU_EXIT:
		default:
			goto done;
	}

	PSPLOG_DEBUG("Opening network connection dialog");
	network_dialog();

	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SSID, &ssid);
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_GATEWAY, &gateway);
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &ip);

	PSPLOG_INFO("connected to %s (%s)", ssid.ssid, gateway.gateway);
	PSPLOG_INFO("get ip: %s", ip.ip);

	/* initialize controller */
	sceCtrlSetSamplingCycle(0); /* in ms: 0=VSYNC */
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	/* initialize and connect to drone */
	PSPLOG_INFO ("initializing drone");
	ret = drone_init (&drone, DRONE_IP, DRONE_DISCOVERY_PORT,
			DRONE_C2D_PORT, DRONE_D2C_PORT);
	if (ret < 0) {
		PSPLOG_ERROR ("failed to initialize drone");
	}

	while (running) {
		SceCtrlData pad;
		SceCtrlLatch latch;
		int yaw = 0;
		int pitch = 0;
		int roll = 0;
		int gaz = 0;

		sceCtrlReadBufferPositive (&pad, 1);
		sceCtrlReadLatch(&latch);

		/* Check triangle and circle transition */
		if ((latch.uiPress & PSP_CTRL_TRIANGLE) &&
				(latch.uiMake & PSP_CTRL_TRIANGLE)) {
			if (is_flying)
				drone_landing (&drone);
			else
				drone_takeoff (&drone);

			is_flying = !is_flying;
		}

		if ((latch.uiPress & PSP_CTRL_CIRCLE) &&
				(latch.uiMake & PSP_CTRL_CIRCLE)) {
			drone_emergency (&drone);
			is_flying = 0;
		}

		if ((latch.uiPress & PSP_CTRL_SELECT) &&
				(latch.uiMake & PSP_CTRL_SELECT))
			drone_flat_trim (&drone);

		/* Send flight control */
		if (pad.Buttons != 0) {
			if (pad.Buttons & PSP_CTRL_CROSS)
				gaz += 50;
			if (pad.Buttons & PSP_CTRL_SQUARE)
				gaz -= 50;

			if (pad.Buttons & PSP_CTRL_LTRIGGER)
				yaw -= 50;
			if (pad.Buttons & PSP_CTRL_RTRIGGER)
				yaw += 50;

			if (pad.Buttons & PSP_CTRL_UP)
				pitch += 50;
			if (pad.Buttons & PSP_CTRL_DOWN)
				pitch -= 50;

			if (pad.Buttons & PSP_CTRL_LEFT)
				roll -= 50;
			if (pad.Buttons & PSP_CTRL_RIGHT)
				roll += 50;
		}
		drone_flight_control (&drone, gaz, yaw, pitch, roll);

//		sceKernelDelayThread (10 * 1000);
	}

	drone_deinit (&drone);

	TTF_CloseFont(font);
	TTF_Quit();
	SDL_Quit();
done:
	network_deinit();
	psplog_deinit();

	sceKernelExitGame();
	return 0;
}

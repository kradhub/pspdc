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

#include <pspuser.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psputility_netconf.h>
#include <pspgu.h>

#include "ui.h"
#include "menu.h"
#include "color.h"
#include "psplog.h"

extern int running;

unsigned int __attribute__((aligned(16))) list[4096];

int
ui_init(UI * ui, int width, int height)
{
	ui->screen = NULL;
	ui->font = NULL;

	ui->screen = SDL_SetVideoMode(width, height, 32,
			SDL_HWSURFACE | SDL_DOUBLEBUF);
	if (ui->screen == NULL)
		goto no_screen;

	SDL_ShowCursor(SDL_DISABLE);

	ui->font = TTF_OpenFont("DejaVuSans.ttf", 16);
	if (ui->font == NULL)
		goto no_font;

	/* initialize controller */
	sceCtrlSetSamplingCycle(0); /* in ms: 0=VSYNC */
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	return 0;

no_screen:
	PSPLOG_ERROR("failed to set screen video mode");
	return -1;

no_font:
	PSPLOG_ERROR("failed to open font");
	return -1;
}

void ui_deinit(UI * ui)
{
	if (ui->font)
		TTF_CloseFont(ui->font);
}

int ui_main_menu_run(UI * ui)
{
	Menu *main_menu;
	SDL_Surface *screen = ui->screen;
	TTF_Font *font = ui->font;
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

/* configuration dialog return 0 when connected
 * 1 when cancelled (ie back)
 */
int
ui_network_dialog_run(UI * ui)
{
	pspUtilityNetconfData conf;
	struct pspUtilityNetconfAdhoc adhoc_params;
	unsigned int swap_count = 0;

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
		swap_count++;

		if (done)
			break;
	}

	/* hack for SDL compatibility.
	 * if it end up on an odd buffer, SDL won't be displayed.
	 * ie SDL will display in an hidden buffer
	 */
	if (swap_count & 1)
		sceGuSwapBuffers();

	return conf.base.result;
}

int
ui_flight_run(UI * ui, Drone * drone)
{
	int is_flying = 0;

	while (running) {
		SceCtrlData pad;
		SceCtrlLatch latch;
		int yaw = 0;
		int pitch = 0;
		int roll = 0;
		int gaz = 0;

		sceCtrlReadBufferPositive (&pad, 1);
		sceCtrlReadLatch(&latch);

		is_flying = (drone->state == DRONE_STATE_TAKING_OFF) ||
			(drone->state == DRONE_STATE_FLYING);

		/* Check triangle and circle transition */
		if ((latch.uiPress & PSP_CTRL_TRIANGLE) &&
				(latch.uiMake & PSP_CTRL_TRIANGLE)) {
			if (is_flying)
				drone_landing (drone);
			else
				drone_takeoff (drone);
		}

		if ((latch.uiPress & PSP_CTRL_CIRCLE) &&
				(latch.uiMake & PSP_CTRL_CIRCLE)) {
			drone_emergency (drone);
			is_flying = 0;
		}

		if ((latch.uiPress & PSP_CTRL_SELECT) &&
				(latch.uiMake & PSP_CTRL_SELECT))
			drone_flat_trim (drone);

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
		drone_flight_control (drone, gaz, yaw, pitch, roll);
	}

	return 0;
}

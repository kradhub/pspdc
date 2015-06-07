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

enum {
	FLIGHT_MENU_QUIT = 0,
};

static int
ui_flight_battery_update(UI * ui, unsigned int percent)
{
	SDL_Surface *text;
	SDL_Rect position;
	const SDL_Color *color;
	char percent_str[5];

	if (percent > 100)
		return -1;

	snprintf(percent_str, 5, "%u%%", percent);

	/* select color according to value */
	if (percent < 10)
		color = &color_red;
	else if (percent < 30)
		color = &color_yellow;
	else
		color = &color_green;

	text = TTF_RenderText_Blended(ui->font, percent_str, *color);
	if (text == NULL)
		goto no_text;

	/* battery is draw on the top right of the screen */
	position.x = ui->screen->w - text->w - 5;
	position.y = 0;

	if (SDL_BlitSurface(text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	SDL_FreeSurface(text);

	return 0;

no_text:
	PSPLOG_ERROR("failed to render text");
	return -1;

blit_failed:
	PSPLOG_ERROR("failed to blit text to screen");
	return -1;
}

static int
ui_flight_state_update (UI * ui, DroneState state)
{
	SDL_Surface *text;
	SDL_Rect position;
	const char *state_str;

	/* state is at top-left of the screen */
	position.x = 5;
	position.y = 0;

	switch (state) {
		case DRONE_STATE_LANDED:
			state_str = "landed";
			break;

		case DRONE_STATE_TAKING_OFF:
			state_str = "taking off";
			break;

		case DRONE_STATE_FLYING:
			state_str = "flying";
			break;

		case DRONE_STATE_LANDING:
			state_str = "landing";
			break;

		case DRONE_STATE_EMERGENCY:
			state_str = "emergency";
			break;

		default:
			state_str = "unknown";
			break;
	}

	text = TTF_RenderText_Blended(ui->font, state_str, color_white);
	if (text == NULL)
		goto no_text;

	if (SDL_BlitSurface(text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	SDL_FreeSurface(text);
	return 0;

no_text:
	PSPLOG_ERROR("failed to render text");
	return -1;

blit_failed:
	PSPLOG_ERROR("failed to blit text to screen");
	return -1;
}

static int
ui_flight_update(UI * ui, Drone * drone)
{
	SDL_Rect top_bar;
	int ret;

	/* clear screen */
	SDL_FillRect (ui->screen, NULL,
			SDL_MapRGB(ui->screen->format, 28, 142, 207));

	/* draw top bar */
	top_bar.x = 0;
	top_bar.y = 0;
	top_bar.w = 480;
	top_bar.h = 20;
	SDL_FillRect (ui->screen, &top_bar,
			SDL_MapRGB(ui->screen->format, 0, 0, 0));

	ret = ui_flight_battery_update (ui, drone->battery);
	ret = ui_flight_state_update (ui, drone->state);

	sceDisplayWaitVblankStart();
	SDL_Flip(ui->screen);

	return ret;
}

static int
ui_flight_menu(UI * ui)
{
	Menu *menu;
	SDL_Surface *surface;
	SDL_Rect position;
	int selected_id = -1;

	menu = menu_new(ui->font, "menu");
	menu_add_entry(menu, FLIGHT_MENU_QUIT, "Return to main menu");

	/* center position in screen */
	position.x = (ui->screen->w - menu_get_width(menu)) / 2;
	position.y = (ui->screen->h - menu_get_height(menu)) / 2;

	while (running) {
		SceCtrlLatch latch;

		surface = menu_render(menu);
		SDL_BlitSurface(surface, NULL, ui->screen, &position);
		SDL_FreeSurface(surface);

		sceDisplayWaitVblankStart();
		SDL_Flip(ui->screen);

		sceCtrlReadLatch(&latch);
		if ((latch.uiPress & PSP_CTRL_UP) &&
				(latch.uiMake & PSP_CTRL_UP))
			menu_select_prev_entry(menu);

		if ((latch.uiPress & PSP_CTRL_DOWN) &&
				(latch.uiMake & PSP_CTRL_DOWN))
			menu_select_next_entry(menu);

		if ((latch.uiPress & PSP_CTRL_CROSS) &&
				(latch.uiMake & PSP_CTRL_CROSS)) {
			selected_id = menu_get_selected_id(menu);
			break;
		}

		if ((latch.uiPress & PSP_CTRL_START) &&
				(latch.uiMake & PSP_CTRL_START))
			break;
	}

	menu_free(menu);
	return selected_id;
}

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
	int ret = 0;
	int is_flying = 0;

	while (running) {
		SceCtrlData pad;
		SceCtrlLatch latch;
		int yaw = 0;
		int pitch = 0;
		int roll = 0;
		int gaz = 0;

		ui_flight_update (ui, drone);

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

		if ((latch.uiPress & PSP_CTRL_START) &&
				(latch.uiMake & PSP_CTRL_START)) {
			if (ui_flight_menu(ui) == FLIGHT_MENU_QUIT) {
				ret = FLIGHT_UI_MAIN_MENU;
				break;
			}
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

	return ret;
}

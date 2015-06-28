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

#define EVENT_BUTTON_DOWN(latch, button) \
	(((latch)->uiPress & (button)) && ((latch)->uiMake & (button)))

enum {
	FLIGHT_MENU_QUIT = 0,
	FLIGHT_MENU_HULL_SWITCH = 1,
	FLIGHT_MENU_OUTDOOR_FLIGHT_SWITCH,
	FLIGHT_MENU_AUTOTAKEOFF_SWITCH,
	FLIGHT_MENU_ABSOLUTE_CONTROL_SWITCH,
	FLIGHT_MENU_FLAT_TRIM
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
ui_flight_altitude_update (UI * ui, int altitude)
{
	SDL_Surface *text;
	SDL_Rect position;
	char str[20];

	snprintf(str, 20, "altitude: %d", altitude);
	str[20] = 0;

	text = TTF_RenderText_Blended(ui->font, str, color_white);
	if (text == NULL)
		goto no_text;

	/* position altitude at the top center */
	position.x = (ui->screen->w - text->w) / 2;
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
	top_bar.w = ui->screen->w;
	top_bar.h = 20;
	SDL_FillRect (ui->screen, &top_bar,
			SDL_MapRGB(ui->screen->format, 0, 0, 0));

	ret = ui_flight_battery_update (ui, drone->battery);
	ret = ui_flight_state_update (ui, drone->state);
	ret = ui_flight_altitude_update (ui, drone->altitude);

	sceDisplayWaitVblankStart();
	SDL_Flip(ui->screen);

	return ret;
}

static void
on_hull_switch_toggle(MenuSwitchEntry * entry, void * userdata)
{
	Drone *drone = (Drone *) userdata;
	int value = menu_switch_entry_get_active(entry);

	if (value != drone->hull)
		drone_hull_set_active(drone, value);
}

static void
on_outdoor_flight_switch_toggle(MenuSwitchEntry * entry, void * userdata)
{
	Drone *drone = (Drone *) userdata;
	int value = menu_switch_entry_get_active(entry);

	if (value != drone->outdoor)
		drone_outdoor_flight_set_active(drone, value);
}

static void
on_autotakeoff_switch_toggle(MenuSwitchEntry * entry, void * userdata)
{
	Drone *drone = (Drone *) userdata;
	int value = menu_switch_entry_get_active(entry);

	if (value != drone->autotakeoff)
		drone_autotakeoff_set_active(drone, value);
}

static void
on_absolute_control_switch_toggle(MenuSwitchEntry * entry, void * userdata)
{
	Drone *drone = (Drone *) userdata;
	int value = menu_switch_entry_get_active(entry);

	if (value != drone->abs_control)
		drone_absolute_control_set_active(drone, value);
}

static int
ui_flight_menu(UI * ui, Drone * drone)
{
	Menu *menu;
	MenuButtonEntry *main_menu_button;
	MenuSwitchEntry *hull_switch;
	MenuSwitchEntry *outdoor_flight_switch;
	MenuSwitchEntry *autotakeoff_switch;
	MenuSwitchEntry *absolute_control_switch;
	SDL_Rect position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	int selected_id = -1;

	menu = menu_new(ui->font, MENU_CANCEL_ON_START);
	main_menu_button = menu_button_entry_new(FLIGHT_MENU_QUIT,
			"Return to main menu");

	/* button to do flat trim */
	{
		MenuButtonEntry *button;

		button = menu_button_entry_new(FLIGHT_MENU_FLAT_TRIM,
				"do flat trim");
		menu_add_entry(menu, (MenuEntry *) button);
	}

	/* hull presence selection */
	hull_switch = menu_switch_entry_new(FLIGHT_MENU_HULL_SWITCH,
			"Hull set");
	menu_switch_entry_set_values_labels(hull_switch, "no", "yes");
	menu_switch_entry_set_active(hull_switch, drone->hull);
	menu_switch_entry_set_toggled_callback(hull_switch,
			on_hull_switch_toggle, drone);

	outdoor_flight_switch =
		menu_switch_entry_new(FLIGHT_MENU_OUTDOOR_FLIGHT_SWITCH,
				"outdoor flight");
	menu_switch_entry_set_values_labels(outdoor_flight_switch, "no", "yes");
	menu_switch_entry_set_active(outdoor_flight_switch, drone->outdoor);
	menu_switch_entry_set_toggled_callback(outdoor_flight_switch,
			on_outdoor_flight_switch_toggle, drone);
	
	autotakeoff_switch = menu_switch_entry_new(FLIGHT_MENU_AUTOTAKEOFF_SWITCH,
			"Auto takeoff");
	menu_switch_entry_set_values_labels(autotakeoff_switch, "no", "yes");
	menu_switch_entry_set_active(autotakeoff_switch, drone->autotakeoff);
	menu_switch_entry_set_toggled_callback(autotakeoff_switch,
			on_autotakeoff_switch_toggle, drone);

	absolute_control_switch = menu_switch_entry_new(FLIGHT_MENU_ABSOLUTE_CONTROL_SWITCH,
			"Absolute control");
	menu_switch_entry_set_values_labels(absolute_control_switch, "no", "yes");
	menu_switch_entry_set_active(absolute_control_switch, drone->abs_control);
	menu_switch_entry_set_toggled_callback(absolute_control_switch,
			on_absolute_control_switch_toggle, drone);

	menu_add_entry(menu, (MenuEntry *) hull_switch);
	menu_add_entry(menu, (MenuEntry *) outdoor_flight_switch);
	menu_add_entry(menu, (MenuEntry *) autotakeoff_switch);
	menu_add_entry(menu, (MenuEntry *) absolute_control_switch);
	menu_add_entry(menu, (MenuEntry *) main_menu_button);

	/* center position in screen */
	position.x = (ui->screen->w - menu_get_width(menu)) / 2;
	position.y = (ui->screen->h - menu_get_height(menu)) / 2;

	/* to fill a rectangle in background to delimitate menu */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width(menu) + 10;
	menu_frame.h = menu_get_height(menu) + 10;
	frame = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect(frame, NULL, SDL_MapRGB(frame->format, 0, 0, 0));
	SDL_SetAlpha(frame, SDL_SRCALPHA, 200);

	while (running) {
		switch (menu_update(menu)) {
			case MENU_STATE_CLOSE:
				selected_id = menu_get_selected_id(menu);
				goto done;
				break;

			case MENU_STATE_VISIBLE:
				/* sync option with drone */
				menu_switch_entry_set_active(hull_switch,
						drone->hull);
				menu_switch_entry_set_active(outdoor_flight_switch,
						drone->outdoor);
				menu_switch_entry_set_active(autotakeoff_switch,
						drone->autotakeoff);
				menu_switch_entry_set_active(absolute_control_switch,
						drone->abs_control);

				SDL_BlitSurface(frame, NULL, ui->screen,
						&menu_frame);
				menu_render_to(menu, ui->screen, &position);
				sceDisplayWaitVblankStart();
				SDL_Flip(ui->screen);
				break;

			case MENU_STATE_CANCELLED:
			default:
				goto done;
				break;

		}
	}

	switch (selected_id) {
		case FLIGHT_MENU_FLAT_TRIM:
			drone_flat_trim(drone);
			break;

		default:
			break;
	}


done:
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
	MenuButtonEntry *connect_button;
	MenuButtonEntry *exit_button;
	SDL_Surface *screen = ui->screen;
	TTF_Font *font = ui->font;
	SDL_Rect position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	int selected_id = -1;

	main_menu = menu_new(font, 0);
	connect_button = menu_button_entry_new(MAIN_MENU_CONNECT,
			"Connect to drone");
	exit_button = menu_button_entry_new(MAIN_MENU_EXIT, "Exit");

	menu_add_entry(main_menu, (MenuEntry *) connect_button);
	menu_add_entry(main_menu, (MenuEntry *) exit_button);

	/* center position in screen */
	position.x = (screen->w - menu_get_width(main_menu)) / 2;
	position.y = (screen->h - menu_get_height(main_menu)) / 2;

	/* delimitate menu with a black rectangle */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width(main_menu) + 10;
	menu_frame.h = menu_get_height(main_menu) + 10;
	frame = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect(frame, NULL, SDL_MapRGB(frame->format, 0, 0, 0));
	SDL_SetAlpha(frame, SDL_SRCALPHA, 200);

	while (running) {
		switch (menu_update(main_menu)) {
			case MENU_STATE_CLOSE:
				selected_id = menu_get_selected_id(main_menu);
				goto done;
				break;
			default:
				/* update screen */
				SDL_FillRect (ui->screen, NULL,
						SDL_MapRGB(ui->screen->format, 28, 142, 207));
				SDL_BlitSurface(frame, NULL, screen, &menu_frame);
				menu_render_to(main_menu, screen, &position);
				sceDisplayWaitVblankStart();
				SDL_Flip(screen);
				break;
		}
	}

done:
	SDL_FreeSurface(frame);
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
		if (EVENT_BUTTON_DOWN(&latch, PSP_CTRL_TRIANGLE)) {
			if (is_flying)
				drone_landing (drone);
			else
				drone_takeoff (drone);
		}

		if (EVENT_BUTTON_DOWN(&latch, PSP_CTRL_CIRCLE)) {
			drone_emergency (drone);
			is_flying = 0;
		}

		if (EVENT_BUTTON_DOWN(&latch, PSP_CTRL_START)) {
			if (ui_flight_menu(ui, drone) == FLIGHT_MENU_QUIT) {
				ret = FLIGHT_UI_MAIN_MENU;
				break;
			}
		}

		/* Send flight control */
		if (pad.Buttons != 0) {
			if (pad.Buttons & PSP_CTRL_CROSS)
				gaz += 75;
			if (pad.Buttons & PSP_CTRL_SQUARE)
				gaz -= 75;

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

		if (gaz || yaw || pitch || roll)
			drone_flight_control (drone, gaz, yaw, pitch, roll);
	}

	return ret;
}

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

enum
{
	FLIGHT_MAIN_MENU_QUIT = 0,
	FLIGHT_MAIN_MENU_FLAT_TRIM,
	FLIGHT_MAIN_MENU_PILOTING_SETTINGS,
	FLIGHT_MAIN_MENU_CONTROLS_SETTINGS,
	FLIGHT_MAIN_MENU_DRONE_INFO,
};

enum
{
	PILOTING_SETTINGS_MENU_HULL = 0,
	PILOTING_SETTINGS_MENU_OUTDOOR_FLIGHT,
	PILOTING_SETTINGS_MENU_ALTITUDE_LIMIT,
	PILOTING_SETTINGS_MENU_VERTICAL_SPEED_LIMIT,
	PILOTING_SETTINGS_MENU_ROTATION_SPEED_LIMIT,
	PILOTING_SETTINGS_MENU_TILT_LIMIT,
};

enum
{
	CONTROLS_SETTINGS_YAW = 0,
	CONTROLS_SETTINGS_PITCH,
	CONTROLS_SETTINGS_ROLL,
	CONTROLS_SETTINGS_GAZ,
	CONTROLS_SETTINGS_SELECT_BINDING,
};

enum
{
	SELECT_BIND_TAKE_PICTURE,
	SELECT_BIND_FLIP_FRONT,
	SELECT_BIND_FLIP_BACK,
	SELECT_BIND_FLIP_RIGHT,
	SELECT_BIND_FLIP_LEFT,
};

enum
{
	DRONE_INFO_MENU_DRONE_HW = 0,
	DRONE_INFO_MENU_DRONE_SW,
	DRONE_INFO_MENU_ARCOMMAND_VERSION,
};

static int
ui_flight_battery_update (UI * ui, unsigned int percent)
{
	SDL_Surface *text;
	SDL_Rect position;
	const SDL_Color *color;
	char percent_str[5];

	if (percent > 100)
		return -1;

	snprintf (percent_str, 5, "%u%%", percent);

	/* select color according to value */
	if (percent < 10)
		color = &color_red;
	else if (percent < 30)
		color = &color_yellow;
	else
		color = &color_green;

	text = TTF_RenderText_Blended (ui->font, percent_str, *color);
	if (text == NULL)
		goto no_text;

	/* battery is draw on the top right of the screen */
	position.x = ui->screen->w - text->w - 5;
	position.y = 0;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	SDL_FreeSurface (text);

	return 0;

no_text:
	PSPLOG_ERROR ("failed to render text");
	return -1;

blit_failed:
	PSPLOG_ERROR ("failed to blit text to screen");
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

	text = TTF_RenderText_Blended (ui->font, state_str, color_white);
	if (text == NULL)
		goto no_text;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	SDL_FreeSurface (text);
	return 0;

no_text:
	PSPLOG_ERROR ("failed to render text");
	return -1;

blit_failed:
	PSPLOG_ERROR ("failed to blit text to screen");
	return -1;
}

static int
ui_flight_altitude_update (UI * ui, int altitude)
{
	SDL_Surface *text;
	SDL_Rect position;
	char str[20];

	snprintf (str, 20, "altitude: %d", altitude);
	str[19] = 0;

	text = TTF_RenderText_Blended (ui->font, str, color_white);
	if (text == NULL)
		goto no_text;

	/* position altitude at the top center */
	position.x = (ui->screen->w - text->w) / 2;
	position.y = 0;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	SDL_FreeSurface (text);
	return 0;

no_text:
	PSPLOG_ERROR ("failed to render text");
	return -1;

blit_failed:
	PSPLOG_ERROR ("failed to blit text to screen");
	return -1;
}

#define BUFFER_LEN 255

static SDL_Surface *
ui_render_text (UI * ui, const SDL_Color * color, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFFER_LEN] = { 0, };

	va_start (ap, fmt);
	vsnprintf (buf, BUFFER_LEN, fmt, ap);
	va_end (ap);
	buf[BUFFER_LEN - 1] = 0;

	return TTF_RenderText_Blended (ui->font, buf, *color);
}

static int
ui_flight_gps_update (UI * ui, Drone * drone)
{
	SDL_Surface *text;
	SDL_Rect position;

	text = ui_render_text (ui, &color_black, "gps: %s", drone->gps_fixed ?
			"yes" : "no");
	if (text == NULL)
		goto no_text;

	/* position gps at the top-left of the screen, below top bar */
	position.x = 0;
	position.y = 20;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	position.y += position.h;
	SDL_FreeSurface (text);

	text = ui_render_text (ui, &color_black, "latitude: %lf",
			drone->gps_latitude);
	if (text == NULL)
		goto no_text;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	position.y += position.h;
	SDL_FreeSurface (text);

	text = ui_render_text (ui, &color_black, "longitude: %lf",
			drone->gps_longitude);
	if (text == NULL)
		goto no_text;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	position.y += position.h;
	SDL_FreeSurface (text);

	text = ui_render_text (ui, &color_black, "altitude: %lf",
			drone->gps_altitude);
	if (text == NULL)
		goto no_text;

	if (SDL_BlitSurface (text, NULL, ui->screen, &position) < 0)
		goto blit_failed;

	position.y += position.h;
	SDL_FreeSurface (text);

	return 0;

no_text:
	PSPLOG_ERROR ("failed to render text");
	return -1;

blit_failed:
	PSPLOG_ERROR ("failed to blit text to screen");
	SDL_FreeSurface (text);
	return -1;
}
static int
ui_flight_update (UI * ui, Drone * drone)
{
	SDL_Rect top_bar;
	int ret;

	/* clear screen */
	SDL_FillRect (ui->screen, NULL,
			SDL_MapRGB (ui->screen->format, 28, 142, 207));

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
	ret = ui_flight_gps_update (ui, drone);

	return ret;
}

static void
on_hull_switch_toggle (MenuSwitchEntry * entry, void * userdata)
{
	Drone *drone = (Drone *) userdata;
	unsigned int value = menu_switch_entry_get_active (entry);

	if (value != drone->hull)
		drone_hull_set_active (drone, value);
}

static void
on_outdoor_flight_switch_toggle (MenuSwitchEntry * entry, void * userdata)
{
	Drone *drone = (Drone *) userdata;
	unsigned int value = menu_switch_entry_get_active (entry);

	if (value != drone->outdoor)
		drone_outdoor_flight_set_active (drone, value);
}

static int
ui_piloting_settings_menu (UI * ui, Drone * drone)
{
	Menu *menu;
	MenuSwitchEntry *hull_switch;
	MenuSwitchEntry *outdoor_flight_switch;
	MenuScaleEntry *altitude_limit_scale;
	MenuScaleEntry *vertical_limit_scale;
	MenuScaleEntry *rotation_limit_scale;
	MenuScaleEntry *tilt_limit_scale;
	SDL_Rect position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	MenuState ret;

	menu = menu_new (ui->font, MENU_CANCEL_ON_START | MENU_BACK_ON_CIRCLE);

	/* hull presence selection */
	hull_switch = menu_switch_entry_new (PILOTING_SETTINGS_MENU_HULL,
			"Hull set");
	menu_switch_entry_set_values_labels (hull_switch, "no", "yes");
	menu_switch_entry_set_active (hull_switch, drone->hull);
	menu_switch_entry_set_toggled_callback (hull_switch,
			on_hull_switch_toggle, drone);

	/* outdoor flight */
	outdoor_flight_switch =
		menu_switch_entry_new (PILOTING_SETTINGS_MENU_OUTDOOR_FLIGHT,
				"outdoor flight");
	menu_switch_entry_set_values_labels (outdoor_flight_switch, "no", "yes");
	menu_switch_entry_set_active (outdoor_flight_switch, drone->outdoor);
	menu_switch_entry_set_toggled_callback (outdoor_flight_switch,
			on_outdoor_flight_switch_toggle, drone);

	/* altitude limit settings */
	altitude_limit_scale =
		menu_scale_entry_new (PILOTING_SETTINGS_MENU_ALTITUDE_LIMIT,
				"altitude limit (m)", drone->altitude_limit.min,
				drone->altitude_limit.max);
	menu_scale_entry_set_value (altitude_limit_scale,
			drone->altitude_limit.current);

	/* vertical speed limit settings */
	vertical_limit_scale =
		menu_scale_entry_new (PILOTING_SETTINGS_MENU_VERTICAL_SPEED_LIMIT,
				"vertical speed limit (m/s)",
				drone->vertical_speed_limit.min,
				drone->vertical_speed_limit.max);
	menu_scale_entry_set_value (vertical_limit_scale,
			drone->vertical_speed_limit.current);

	/* rotation speed limit settings */
	rotation_limit_scale =
		menu_scale_entry_new (PILOTING_SETTINGS_MENU_ROTATION_SPEED_LIMIT,
				"rotation speed limit (deg/s)",
				drone->rotation_speed_limit.min,
				drone->rotation_speed_limit.max);
	menu_scale_entry_set_value (rotation_limit_scale,
			drone->rotation_speed_limit.current);

	/* rotation speed limit settings */
	tilt_limit_scale =
		menu_scale_entry_new (PILOTING_SETTINGS_MENU_TILT_LIMIT,
				"tilt limit (deg)",
				drone->tilt_limit.min,
				drone->tilt_limit.max);
	menu_scale_entry_set_value (tilt_limit_scale,
			drone->tilt_limit.current);

	menu_add_entry (menu, (MenuEntry *) hull_switch);
	menu_add_entry (menu, (MenuEntry *) outdoor_flight_switch);
	menu_add_entry (menu, (MenuEntry *) altitude_limit_scale);
	menu_add_entry (menu, (MenuEntry *) vertical_limit_scale);
	menu_add_entry (menu, (MenuEntry *) rotation_limit_scale);
	menu_add_entry (menu, (MenuEntry *) tilt_limit_scale);

	/* center position in screen */
	position.x = (ui->screen->w - menu_get_width(menu)) / 2;
	position.y = (ui->screen->h - menu_get_height(menu)) / 2;

	/* to fill a rectangle in background to delimitate menu */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width (menu) + 10;
	menu_frame.h = menu_get_height (menu) + 10;
	frame = SDL_CreateRGBSurface (SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect (frame, NULL, SDL_MapRGB (frame->format, 0, 0, 0));
	SDL_SetAlpha (frame, SDL_SRCALPHA, 200);

	while (running) {
		/*redraw flight ui */
		ui_flight_update(ui, drone);

		ret = menu_update (menu);
		switch (ret) {
			case MENU_STATE_VISIBLE:
				/* sync option with drone */
				menu_switch_entry_set_active (hull_switch,
						drone->hull);
				menu_switch_entry_set_active (outdoor_flight_switch,
						drone->outdoor);

				SDL_BlitSurface (frame, NULL, ui->screen,
						&menu_frame);
				menu_render_to (menu, ui->screen, &position);
				sceDisplayWaitVblankStart ();
				SDL_Flip (ui->screen);
				break;

			case MENU_STATE_CLOSE:
			case MENU_STATE_CANCELLED:
			default:
				goto done;
				break;

		}
	}

done:
	/* send new scale entries value to drone, not done in callback
	 * to avoid flooding drone and because it can't work well in our side
	 * due to local update and callback for drones for previously sent
	 * values. */
	drone_altitude_limit_set (drone,
			menu_scale_entry_get_value (altitude_limit_scale));
	drone_vertical_speed_limit_set (drone,
			menu_scale_entry_get_value (vertical_limit_scale));
	drone_rotation_speed_limit_set (drone,
			menu_scale_entry_get_value (rotation_limit_scale));
	drone_max_tilt_set (drone,
			menu_scale_entry_get_value (tilt_limit_scale));

	menu_free (menu);
	return ret;
}

static int
ui_controls_settings_menu (UI * ui, Drone * drone)
{
	Menu *menu;
	MenuScaleEntry *yaw_scale;
	MenuScaleEntry *pitch_scale;
	MenuScaleEntry *roll_scale;
	MenuScaleEntry *gaz_scale;
	MenuComboBoxEntry *select_binding;
	SDL_Rect position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	MenuState ret;

	menu = menu_new (ui->font, MENU_CANCEL_ON_START | MENU_BACK_ON_CIRCLE);

	yaw_scale = menu_scale_entry_new (CONTROLS_SETTINGS_YAW, "yaw", 0, 100);
	menu_scale_entry_set_value (yaw_scale, ui->setting_yaw);

	pitch_scale = menu_scale_entry_new (CONTROLS_SETTINGS_PITCH, "pitch", 0,
			100);
	menu_scale_entry_set_value (pitch_scale, ui->setting_pitch);

	roll_scale = menu_scale_entry_new (CONTROLS_SETTINGS_ROLL, "roll", 0,
			100);
	menu_scale_entry_set_value (roll_scale, ui->setting_roll);

	gaz_scale = menu_scale_entry_new (CONTROLS_SETTINGS_GAZ, "gaz", 0, 100);
	menu_scale_entry_set_value (gaz_scale, ui->setting_gaz);

	select_binding =
		menu_combo_box_entry_new (CONTROLS_SETTINGS_SELECT_BINDING,
				"select binding");
	menu_combo_box_entry_append (select_binding, SELECT_BIND_TAKE_PICTURE,
			"take picture");
	menu_combo_box_entry_append (select_binding, SELECT_BIND_FLIP_FRONT,
			"front flip");
	menu_combo_box_entry_append (select_binding, SELECT_BIND_FLIP_BACK,
			"back flip");
	menu_combo_box_entry_append (select_binding, SELECT_BIND_FLIP_LEFT,
			"left flip");
	menu_combo_box_entry_append (select_binding, SELECT_BIND_FLIP_RIGHT,
			"right flip");
	menu_combo_box_entry_set_value (select_binding,
			ui->setting_select_binding);

	menu_add_entry (menu, (MenuEntry *) yaw_scale);
	menu_add_entry (menu, (MenuEntry *) pitch_scale);
	menu_add_entry (menu, (MenuEntry *) roll_scale);
	menu_add_entry (menu, (MenuEntry *) gaz_scale);
	menu_add_entry (menu, (MenuEntry *) select_binding);

	/* center position in screen */
	position.x = (ui->screen->w - menu_get_width (menu)) / 2;
	position.y = (ui->screen->h - menu_get_height (menu)) / 2;

	/* to fill a rectangle in background to delimitate menu */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width (menu) + 10;
	menu_frame.h = menu_get_height (menu) + 10;
	frame = SDL_CreateRGBSurface (SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect (frame, NULL, SDL_MapRGB (frame->format, 0, 0, 0));
	SDL_SetAlpha (frame, SDL_SRCALPHA, 200);

	while (running) {
		/*redraw flight ui */
		ui_flight_update(ui, drone);

		ret = menu_update (menu);
		switch (ret) {
			case MENU_STATE_VISIBLE:
				SDL_BlitSurface (frame, NULL, ui->screen,
						&menu_frame);
				menu_render_to (menu, ui->screen, &position);
				sceDisplayWaitVblankStart ();
				SDL_Flip (ui->screen);
				break;

			case MENU_STATE_CLOSE:
			case MENU_STATE_CANCELLED:
			default:
				goto done;
				break;

		}
	}

done:
	/* store value to ui */
	ui->setting_yaw = menu_scale_entry_get_value (yaw_scale);
	ui->setting_pitch = menu_scale_entry_get_value (pitch_scale);
	ui->setting_roll = menu_scale_entry_get_value (roll_scale);
	ui->setting_gaz = menu_scale_entry_get_value (gaz_scale);
	ui->setting_select_binding =
		menu_combo_box_entry_get_value (select_binding);

	menu_free (menu);
	return ret;
}

static int
ui_drone_info_menu (UI * ui, Drone * drone)
{
	Menu *menu;
	MenuLabelEntry *drone_sw;
	MenuLabelEntry *drone_hw;
	MenuLabelEntry *arcommand_version;
	char tmp[128] = { 0, };
	SDL_Rect position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	MenuState ret;

	menu = menu_new (ui->font, MENU_CANCEL_ON_START | MENU_BACK_ON_CIRCLE);

	snprintf (tmp, 127, "Drone HW: %s", drone->hardware_version);
	drone_hw = menu_label_entry_new (DRONE_INFO_MENU_DRONE_HW, tmp);

	snprintf (tmp, 127, "Drone SW: %s", drone->software_version);
	drone_sw = menu_label_entry_new (DRONE_INFO_MENU_DRONE_HW, tmp);

	snprintf (tmp, 127, "Protocol version: %s", drone->arcommand_version);
	arcommand_version =
		menu_label_entry_new (DRONE_INFO_MENU_ARCOMMAND_VERSION, tmp);

	menu_add_entry (menu, (MenuEntry *) drone_hw);
	menu_add_entry (menu, (MenuEntry *) drone_sw);
	menu_add_entry (menu, (MenuEntry *) arcommand_version);

	/* center position in screen */
	position.x = (ui->screen->w - menu_get_width (menu)) / 2;
	position.y = (ui->screen->h - menu_get_height (menu)) / 2;

	/* to fill a rectangle in background to delimitate menu */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width (menu) + 10;
	menu_frame.h = menu_get_height (menu) + 10;
	frame = SDL_CreateRGBSurface (SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect (frame, NULL, SDL_MapRGB (frame->format, 0, 0, 0));
	SDL_SetAlpha (frame, SDL_SRCALPHA, 200);

	while (running) {
		/*redraw flight ui */
		ui_flight_update(ui, drone);

		ret = menu_update (menu);
		switch (ret) {
			case MENU_STATE_VISIBLE:
				SDL_BlitSurface (frame, NULL, ui->screen,
						&menu_frame);
				menu_render_to (menu, ui->screen, &position);
				sceDisplayWaitVblankStart ();
				SDL_Flip (ui->screen);
				break;

			case MENU_STATE_CANCELLED:
			case MENU_STATE_CLOSE:
			default:
				goto done;
				break;
		}
	}

done:
	menu_free (menu);
	return ret;
}

static int
ui_flight_main_menu (UI * ui, Drone * drone)
{
	Menu *menu;
	MenuButtonEntry *quit;
	MenuButtonEntry *flat_trim;
	MenuButtonEntry *piloting_settings;
	MenuButtonEntry *controls_settings;
	MenuButtonEntry *drone_info;
	SDL_Rect position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	int selected_id = -1;
	MenuState submenu_state;

	menu = menu_new (ui->font, MENU_CANCEL_ON_START | MENU_BACK_ON_CIRCLE);

	quit = menu_button_entry_new (FLIGHT_MAIN_MENU_QUIT,
			"Return to main menu");
	flat_trim = menu_button_entry_new (FLIGHT_MAIN_MENU_FLAT_TRIM,
			"Do flat trim");
	piloting_settings = menu_button_entry_new (
			FLIGHT_MAIN_MENU_PILOTING_SETTINGS,
			"Piloting settings");
	controls_settings =
		menu_button_entry_new (FLIGHT_MAIN_MENU_CONTROLS_SETTINGS,
			"Controls settings");
	drone_info = menu_button_entry_new (FLIGHT_MAIN_MENU_DRONE_INFO,
			"Drone information");

	menu_add_entry (menu, (MenuEntry *) flat_trim);
	menu_add_entry (menu, (MenuEntry *) piloting_settings);
	menu_add_entry (menu, (MenuEntry *) controls_settings);
	menu_add_entry (menu, (MenuEntry *) drone_info);
	menu_add_entry (menu, (MenuEntry *) quit);

	/* center position in screen */
	position.x = (ui->screen->w - menu_get_width (menu)) / 2;
	position.y = (ui->screen->h - menu_get_height (menu)) / 2;

	/* to fill a rectangle in background to delimitate menu */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width (menu) + 10;
	menu_frame.h = menu_get_height (menu) + 10;
	frame = SDL_CreateRGBSurface (SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect (frame, NULL, SDL_MapRGB (frame->format, 0, 0, 0));
	SDL_SetAlpha (frame, SDL_SRCALPHA, 200);

mm_display:
	while (running) {
		MenuCloseResult res;

		selected_id = -1;
		/*redraw flight ui */
		ui_flight_update(ui, drone);

		switch (menu_update (menu)) {
			case MENU_STATE_CLOSE:
				selected_id = menu_get_selected_id (menu);
				res = menu_get_close_result (menu);

				if (res == MENU_CLOSE_RESULT_BACK)
					selected_id = -1;
				goto done;
				break;

			case MENU_STATE_VISIBLE:
				SDL_BlitSurface (frame, NULL, ui->screen,
						&menu_frame);
				menu_render_to (menu, ui->screen, &position);
				sceDisplayWaitVblankStart ();
				SDL_Flip (ui->screen);
				break;

			case MENU_STATE_CANCELLED:
			default:
				goto done;
		}
	}

done:
	switch (selected_id) {
		case FLIGHT_MAIN_MENU_FLAT_TRIM:
			drone_flat_trim (drone);
			submenu_state = MENU_STATE_CLOSE;
			break;

		case FLIGHT_MAIN_MENU_PILOTING_SETTINGS:
			submenu_state = ui_piloting_settings_menu (ui, drone);
			break;

		case FLIGHT_MAIN_MENU_CONTROLS_SETTINGS:
			submenu_state = ui_controls_settings_menu (ui, drone);
			break;

		case FLIGHT_MAIN_MENU_DRONE_INFO:
			submenu_state = ui_drone_info_menu (ui, drone);
			break;

		default:
			submenu_state = MENU_STATE_CANCELLED;
			break;
	}

	if (submenu_state == MENU_STATE_CLOSE)
		goto mm_display;

	menu_free (menu);
	return selected_id;
}

int
ui_init (UI * ui, int width, int height)
{
	ui->screen = NULL;
	ui->font = NULL;

	ui->screen = SDL_SetVideoMode (width, height, 32,
			SDL_HWSURFACE | SDL_DOUBLEBUF);
	if (ui->screen == NULL)
		goto no_screen;

	SDL_ShowCursor (SDL_DISABLE);

	ui->font = TTF_OpenFont ("DejaVuSans.ttf", 16);
	if (ui->font == NULL)
		goto no_font;

	/* initialize controller */
	sceCtrlSetSamplingCycle (0); /* in ms: 0=VSYNC */
	sceCtrlSetSamplingMode (PSP_CTRL_MODE_ANALOG);

	ui->setting_yaw = 50;
	ui->setting_pitch = 50;
	ui->setting_roll = 50;
	ui->setting_gaz = 75;
	ui->setting_select_binding = SELECT_BIND_TAKE_PICTURE;

	return 0;

no_screen:
	PSPLOG_ERROR ("failed to set screen video mode");
	return -1;

no_font:
	PSPLOG_ERROR ("failed to open font");
	return -1;
}

void
ui_deinit(UI * ui)
{
	if (ui->font)
		TTF_CloseFont (ui->font);
}

int
ui_main_menu_run (UI * ui)
{
	Menu *main_menu;
	MenuButtonEntry *connect_button;
	MenuButtonEntry *exit_button;
	SDL_Surface *screen = ui->screen;
	SDL_Surface *title;
	TTF_Font *font = ui->font;
	SDL_Rect position;
	SDL_Rect title_position;
	SDL_Surface *frame;
	SDL_Rect menu_frame;
	int selected_id = -1;

	/* make title and center it at screen top */
	title = ui_render_text (ui, &color_black, "PSP Drone Control");
	title_position.x = (screen->w - title->w) / 2;
	title_position.y = 20;

	main_menu = menu_new (font, 0);
	connect_button = menu_button_entry_new (MAIN_MENU_CONNECT,
			"Connect to drone");
	exit_button = menu_button_entry_new (MAIN_MENU_EXIT, "Exit");

	menu_add_entry (main_menu, (MenuEntry *) connect_button);
	menu_add_entry (main_menu, (MenuEntry *) exit_button);

	/* center position in screen */
	position.x = (screen->w - menu_get_width (main_menu)) / 2;
	position.y = (screen->h - menu_get_height (main_menu)) / 2;

	/* delimitate menu with a black rectangle */
	menu_frame.x = position.x - 5;
	menu_frame.y = position.y - 5;
	menu_frame.w = menu_get_width (main_menu) + 10;
	menu_frame.h = menu_get_height (main_menu) + 10;
	frame = SDL_CreateRGBSurface (SDL_HWSURFACE | SDL_SRCCOLORKEY | SDL_SRCALPHA,
			menu_frame.w, menu_frame.h, 32, 0, 0, 0, 0);
	SDL_FillRect (frame, NULL, SDL_MapRGB (frame->format, 0, 0, 0));
	SDL_SetAlpha (frame, SDL_SRCALPHA, 200);

	while (running) {
		switch (menu_update (main_menu)) {
			case MENU_STATE_CLOSE:
				selected_id = menu_get_selected_id (main_menu);
				goto done;
				break;
			default:
				/* update screen */
				SDL_FillRect (ui->screen, NULL,
						SDL_MapRGB (ui->screen->format, 28, 142, 207));
				SDL_BlitSurface (title, NULL, screen, &title_position);
				SDL_BlitSurface (frame, NULL, screen, &menu_frame);
				menu_render_to (main_menu, screen, &position);
				sceDisplayWaitVblankStart ();
				SDL_Flip (screen);
				break;
		}
	}

done:
	SDL_FreeSurface (frame);
	menu_free (main_menu);
	return selected_id;
}

/* configuration dialog return 0 when connected
 * 1 when cancelled (ie back)
 */
int
ui_network_dialog_run (UI * ui)
{
	pspUtilityNetconfData conf;
	struct pspUtilityNetconfAdhoc adhoc_params;
	unsigned int swap_count = 0;
	SceCtrlLatch latch;

	memset(&conf, 0, sizeof (conf));
	memset(&adhoc_params, 0, sizeof (adhoc_params));

	conf.base.size = sizeof (conf);
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
		sceGuStart (GU_DIRECT, list);
		sceGuClearColor (0xff554433);
		sceGuClearDepth (0);
		sceGuClear (GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
		sceGuFinish ();
		sceGuSync (0,0);

		switch (sceUtilityNetconfGetStatus ()) {
			case PSP_UTILITY_DIALOG_NONE:
				break;

			case PSP_UTILITY_DIALOG_VISIBLE:
				sceUtilityNetconfUpdate (1);
				break;

			case PSP_UTILITY_DIALOG_QUIT:
				sceUtilityNetconfShutdownStart ();
				break;

			case PSP_UTILITY_DIALOG_FINISHED:
				done = 1;
				break;

			default:
				break;
		}

		sceDisplayWaitVblankStart ();
		sceGuSwapBuffers ();
		swap_count++;

		if (done)
			break;
	}

	/* hack for SDL compatibility.
	 * if it end up on an odd buffer, SDL won't be displayed.
	 * ie SDL will display in an hidden buffer
	 */
	if (swap_count & 1)
		sceGuSwapBuffers ();

	/* message dialog seems to causes strange latch behavior, next read
	 * of latch will contains all button pressed during dialog.
	 * read one to reset it */
	sceCtrlReadLatch (&latch);

	return conf.base.result;
}

void
ui_msg_dialog (UI * ui, const char * msg)
{
	pspUtilityMsgDialogParams params;
	unsigned int swap_count = 0;
	SceCtrlLatch latch;

	memset (&params, 0, sizeof (params));

	params.base.size = sizeof (params);
	params.base.language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	params.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;

	/* Thread priorities */
	params.base.graphicsThread = 17;
	params.base.accessThread = 19;
	params.base.fontThread = 18;
	params.base.soundThread = 16;

	params.mode = PSP_UTILITY_MSGDIALOG_MODE_TEXT;
	params.options = PSP_UTILITY_MSGDIALOG_OPTION_TEXT;
	snprintf (params.message, 512, msg);

	sceUtilityMsgDialogInitStart (&params);

	while (running) {
		int done = 0;

		/* directly use GU to avoid flickering with SDL */
		sceGuStart (GU_DIRECT, list);
		sceGuClearColor (0xff554433);
		sceGuClearDepth (0);
		sceGuClear (GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
		sceGuFinish ();
		sceGuSync (0,0);

		switch (sceUtilityMsgDialogGetStatus ()) {
			case PSP_UTILITY_DIALOG_NONE:
				break;
			case PSP_UTILITY_DIALOG_VISIBLE:
				sceUtilityMsgDialogUpdate (1);
				break;
			case PSP_UTILITY_DIALOG_QUIT:
				sceUtilityMsgDialogShutdownStart ();
				break;
			case PSP_UTILITY_DIALOG_FINISHED:
				done = 1;
				break;
			default:
				break;
		}

		sceDisplayWaitVblankStart ();
		sceGuSwapBuffers ();
		swap_count++;

		if (done)
			break;
	}

	/* hack for SDL compatibility.
	 * if it end up on an odd buffer, SDL won't be displayed.
	 * ie SDL will display in an hidden buffer
	 */
	if (swap_count & 1)
		sceGuSwapBuffers ();

	/* message dialog seems to causes strange latch behavior, next read
	 * of latch will contains all button pressed during dialog.
	 * read one to reset it */
	sceCtrlReadLatch (&latch);
}

int
ui_flight_run (UI * ui, Drone * drone)
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

		if (!drone->connected) {
			ui_msg_dialog (ui, "Connection to drone lost");
			ret = FLIGHT_UI_MAIN_MENU;
			break;
		}

		ui_flight_update (ui, drone);

		sceCtrlReadBufferPositive (&pad, 1);
		sceCtrlReadLatch (&latch);

		is_flying = (drone->state == DRONE_STATE_TAKING_OFF) ||
			(drone->state == DRONE_STATE_FLYING);

		/* Check triangle and circle transition */
		if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_TRIANGLE)) {
			if (is_flying)
				drone_landing (drone);
			else
				drone_takeoff (drone);
		}

		if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_CIRCLE)) {
			drone_emergency (drone);
			is_flying = 0;
		}

		if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_SELECT)) {
			switch (ui->setting_select_binding) {
				case SELECT_BIND_TAKE_PICTURE:
					drone_take_picture (drone);
					break;
				case SELECT_BIND_FLIP_FRONT:
					drone_do_flip (drone, DRONE_FLIP_FRONT);
					break;
				case SELECT_BIND_FLIP_BACK:
					drone_do_flip (drone, DRONE_FLIP_BACK);
					break;
				case SELECT_BIND_FLIP_RIGHT:
					drone_do_flip (drone, DRONE_FLIP_RIGHT);
					break;
				case SELECT_BIND_FLIP_LEFT:
					drone_do_flip (drone, DRONE_FLIP_LEFT);
					break;
				default:
					break;
			}
		}

		if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_START)) {
			if (ui_flight_main_menu (ui, drone) ==
					FLIGHT_MAIN_MENU_QUIT) {
				ret = FLIGHT_UI_MAIN_MENU;
				break;
			}
		}

		/* Send flight control */
		if (pad.Buttons != 0) {
			if (pad.Buttons & PSP_CTRL_CROSS)
				gaz += ui->setting_gaz;
			if (pad.Buttons & PSP_CTRL_SQUARE)
				gaz -= ui->setting_gaz;

			if (pad.Buttons & PSP_CTRL_LTRIGGER)
				yaw -= ui->setting_yaw;
			if (pad.Buttons & PSP_CTRL_RTRIGGER)
				yaw += ui->setting_yaw;

			if (pad.Buttons & PSP_CTRL_UP)
				pitch += ui->setting_pitch;
			if (pad.Buttons & PSP_CTRL_DOWN)
				pitch -= ui->setting_pitch;

			if (pad.Buttons & PSP_CTRL_LEFT)
				roll -= ui->setting_roll;
			if (pad.Buttons & PSP_CTRL_RIGHT)
				roll += ui->setting_roll;
		}

		if (gaz || yaw || pitch || roll)
			drone_flight_control (drone, gaz, yaw, pitch, roll);

		sceDisplayWaitVblankStart ();
		SDL_Flip (ui->screen);
	}

	return ret;
}

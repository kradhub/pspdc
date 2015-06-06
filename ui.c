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

#include <pspdisplay.h>
#include <pspctrl.h>

#include "ui.h"
#include "menu.h"
#include "color.h"
#include "psplog.h"

extern int running;

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

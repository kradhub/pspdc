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

#include <string.h>

#include "color.h"
#include "menu.h"
#include "psplog.h"

typedef struct _MenuEntry MenuEntry;

struct _MenuEntry
{
	int id;
	char *title;
	SDL_Surface *surface;

	MenuEntry *prev;
	MenuEntry *next;
};

struct _Menu
{
	char *title;
	TTF_Font *font;

	/* not selected text color */
	SDL_Color default_color;

	/* selected text color */
	SDL_Color selected_color;

	/* must update all texture */
	int updated;

	MenuEntry *head;
	MenuEntry *tail;
	MenuEntry *selected;

	unsigned int width;
	unsigned int height;
};

static void
menu_entry_replace_surface(MenuEntry *entry, SDL_Surface * new_surface)
{
	if (entry->surface)
		SDL_FreeSurface(entry->surface);

	entry->surface = new_surface;
}

static int
menu_entry_render(MenuEntry * entry, TTF_Font * font, const SDL_Color * color)
{
	SDL_Surface *surface;

	surface = TTF_RenderText_Blended(font, entry->title, *color);
	if (!surface)
		return -1;

	menu_entry_replace_surface(entry, surface);

	return 0;
}

static MenuEntry *
menu_entry_new(int id, const char * title)
{
	MenuEntry *entry;

	entry = malloc(sizeof(MenuEntry));
	if (!entry)
		return NULL;

	entry->id = id;
	entry->title = strdup(title);
	entry->prev = entry->next = NULL;
	entry->surface = NULL;

	return entry;
}

static void
menu_entry_free(MenuEntry * entry)
{
	if (entry->title)
		free(entry->title);

	if (entry->surface)
		free(entry->surface);

	free(entry);
}

/*
 * Menu helpers
 */
static void
menu_refresh_all_entries(Menu * menu)
{
	MenuEntry *e;

	for (e = menu->head; e != NULL; e = e->next) {
		menu_entry_render(e, menu->font, &menu->default_color);
	}
}

static MenuEntry *
menu_get_entry_by_id(Menu * menu, int id)
{
	MenuEntry *res = NULL;
	MenuEntry *e;

	for (e = menu->head; e != NULL; e = e->next) {
		if (e->id == id) {
			res = e;
			break;
		}
	}

	return res;
}

static int
menu_select_entry_helper(Menu * menu, MenuEntry * entry)
{
	SDL_Surface *prev_selected = NULL;
	SDL_Surface *new_selected = NULL;

	if (menu->selected) {
		/* make previous selected entry back to normal */
		prev_selected = TTF_RenderText_Blended(menu->font,
				menu->selected->title, menu->default_color);
		if (prev_selected == NULL)
			goto render_failed;
	}

	new_selected = TTF_RenderText_Blended(menu->font, entry->title,
			menu->selected_color);
	if (new_selected == NULL)
		goto render_failed;

	if (menu->selected)
		menu_entry_replace_surface(menu->selected, prev_selected);

	menu_entry_replace_surface(entry, new_selected);
	menu->selected = entry;

	return 0;

render_failed:
	PSPLOG_ERROR("menu_select_entry: failed to render one entry");
	if (prev_selected)
		SDL_FreeSurface(prev_selected);

	return -1;
}


/*
 * API
 */
Menu *
menu_new(TTF_Font * font, const char * title)
{
	Menu *menu;

	menu = malloc(sizeof(Menu));
	if (!menu)
		return NULL;

	menu->title = NULL;
	menu->font = font;
	menu->head = NULL;
	menu->tail = NULL;
	menu->selected = NULL;
	menu->updated = 0;
	menu->default_color = color_white;
	menu->selected_color = color_red;
	menu->width = 0;
	menu->height = 0;

	if (title)
		menu->title = strdup(title);

	return menu;
}

void
menu_free(Menu * menu)
{
	MenuEntry *e;

	if (menu->title)
		free(menu->title);

	for (e = menu->head; e != NULL; e = e->next)
		menu_entry_free(e);
}

void
menu_set_default_color(Menu * menu, const SDL_Color * color)
{
	menu->default_color = *color;
	menu->updated = 1;
}

void
menu_set_selected_color(Menu * menu, const SDL_Color * color)
{
	menu->selected_color = *color;
	menu->updated = 1;
}

int
menu_get_width(Menu * menu)
{
	return menu->width;
}

int
menu_get_height(Menu * menu)
{
	return menu->height;
}

int
menu_get_selected_id(Menu * menu)
{
	if (menu->selected == NULL)
		return -1;

	return menu->selected->id;
}

/* Render menu to a SDL_Surface
 * Don't forget to free returned surface after usage */
SDL_Surface *
menu_render(Menu * menu)
{
	SDL_Surface *surface;
	MenuEntry *e;
	SDL_Rect dest_pos = { 0, 0, 0, 0 };

	if (menu->head == NULL)
		return NULL;

	if (menu->updated) {
		menu_refresh_all_entries(menu);
		menu->updated = 0;
	}

	PSPLOG_DEBUG("menu: creating menu surface of size %ux%u", menu->width,
			menu->height);

	surface = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCALPHA,
			menu->width, menu->height, 32, 0, 0, 0, 0);
	if (surface == NULL)
		return NULL;

	for (e = menu->head; e != NULL; e = e->next) {
		if (e->surface == NULL)
			continue;

		PSPLOG_DEBUG ("menu: blitting entry %p surface (%s) @ (%d,%d)",
				e, e->title, dest_pos.x, dest_pos.y);

		SDL_BlitSurface(e->surface, NULL, surface, &dest_pos);

		/* update position for next entry */
		dest_pos.y += dest_pos.h;
	}

	return surface;
}

int
menu_select_entry(Menu * menu, int id)
{
	MenuEntry *entry;

	entry = menu_get_entry_by_id(menu, id);
	if (entry == NULL)
		return -1;

	return menu_select_entry_helper(menu, entry);
}

int
menu_select_prev_entry(Menu * menu)
{
	if (menu->selected == NULL)
		return -1;

	if (menu->selected->prev == NULL)
		return 0;

	return menu_select_entry_helper(menu, menu->selected->prev);
}

int
menu_select_next_entry(Menu * menu)
{
	if (menu->selected == NULL)
		return -1;

	if (menu->selected->next == NULL)
		return 0;

	return menu_select_entry_helper(menu, menu->selected->next);
}

int
menu_add_entry(Menu * menu, int id, const char * title)
{
	MenuEntry *entry;
	SDL_Color *color;

	entry = menu_entry_new(id, title);
	if (!entry)
		goto cleanup;

	/* first entry will be selected by default */
	if (menu->head == NULL)
		color = &menu->selected_color;
	else
		color = &menu->default_color;

	if (menu_entry_render(entry, menu->font, color) < 0)
		goto cleanup;

	if (entry->surface->w > menu->width)
		menu->width = entry->surface->w;

	menu->height += entry->surface->h;

	/* add it to menu */
	if (menu->tail) {
		menu->tail->next = entry;
		entry->prev = menu->tail;

		menu->tail = entry;
	} else {
		/* first entry, also make it selected */
		menu->head = menu->tail = entry;
		menu->selected = entry;
	}

	return 0;

cleanup:
	menu_entry_free(entry);
	return -1;
}

void
menu_remove_entry(Menu * menu, int id)
{
	MenuEntry *entry;

	entry = menu_get_entry_by_id(menu, id);
	if (entry == NULL)
		return;

	if (entry->prev)
		entry->prev->next = entry->next;

	if (entry->next)
		entry->next->prev = entry->prev;

	if (menu->head == entry)
		menu->head = entry->next;

	if (menu->tail == entry)
		menu->tail = entry->prev;

	if (menu->selected == entry)
		menu->selected = menu->head;

	menu_entry_free(entry);
}

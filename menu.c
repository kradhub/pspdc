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

#include <pspctrl.h>
#include <string.h>

#include "color.h"
#include "menu.h"
#include "psplog.h"

#define MENU_CHOICE_CLOSE 0

#define EVENT_BUTTON_DOWN(latch, button) \
	(((latch)->uiPress & (button)) && ((latch)->uiMake & (button)))

typedef void (*MenuEntryFreeFunction) (MenuEntry * entry);
typedef void (*MenuEntryRenderFunction) (MenuEntry * entry, TTF_Font * font,
		const SDL_Color * color);

struct _MenuEntry
{
	MenuEntryType type;
	Menu *owner;

	int id;
	char *title;
	SDL_Surface *surface;

	MenuEntry *prev;
	MenuEntry *next;

	void (*free) (MenuEntry * entry);
	int (*render) (MenuEntry * entry, TTF_Font * font, const SDL_Color * color);
};

struct _MenuLabelEntry
{
	MenuEntry parent;
};

struct _MenuButtonEntry
{
	MenuEntry parent;
};

struct _MenuSwitchEntry
{
	MenuEntry parent;

	char *on_label;
	char *off_label;

	SDL_Surface *surface_on;
	SDL_Surface *surface_off;

	int active;

	void *userdata;

	void (*toggled) (MenuSwitchEntry * entry, void * userdata);
};

struct _MenuScaleEntry
{
	MenuEntry parent;

	int min;
	int max;
	int current;

	void *userdata;
	MenuScaleEntryValueChangedCallback value_changed;
};

typedef struct _ComboBoxItem ComboBoxItem;

struct _ComboBoxItem
{
	int id;
	char *label;
	ComboBoxItem *prev;
	ComboBoxItem *next;
};

struct _MenuComboBoxEntry
{
	MenuEntry parent;

	ComboBoxItem *items;
	ComboBoxItem *current;
};

struct _Menu
{
	TTF_Font *font;

	int options;

	MenuCloseResult close_result;

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

	/* used to filter controller */
	unsigned int last_ts;
	unsigned int threshold;
};

static int menu_entry_render (MenuEntry * entry, TTF_Font * font,
		const SDL_Color * color);

static void
menu_surface_replace_helper (SDL_Surface ** old, SDL_Surface * new)
{
	if (*old)
		SDL_FreeSurface (*old);

	*old = new;
}

/*
 * Menu helpers
 */
static void
menu_refresh_all_entries (Menu * menu)
{
	MenuEntry *e;

	for (e = menu->head; e != NULL; e = e->next) {
		menu_entry_render (e, menu->font, &menu->default_color);
	}
}

static int
menu_select_entry_helper (Menu * menu, MenuEntry * entry)
{
	int ret;

	/* unselect currently selected entry */
	if (menu->selected) {
		ret = menu_entry_render (menu->selected, menu->font,
				&menu->default_color);

		if (ret < 0)
			goto done;
	}

	ret = menu_entry_render (entry, menu->font, &menu->selected_color);
	menu->selected = entry;

done:
	return ret;
}

static void
menu_repeat_button_reset (Menu * menu, const SceCtrlData * pad)
{
	menu->last_ts = pad->TimeStamp;
	menu->threshold = 500 * 1000; /* 500 ms */
}

static int
menu_is_button_repeated (Menu * menu, const SceCtrlData * pad, int ctrl)
{
	unsigned int diff = (pad->TimeStamp - menu->last_ts);

	if ((pad->Buttons & ctrl) && (diff > menu->threshold)) {
		menu->last_ts = pad->TimeStamp;

		if (menu->threshold > 200 * 1000)
			menu->threshold -= 200 * 1000; /* 200 ms */

		return 1;
	}

	return 0;
}

/*
 * API
 */
Menu *
menu_new (TTF_Font * font, int options)
{
	Menu *menu;

	menu = malloc (sizeof(Menu));
	if (!menu)
		return NULL;

	menu->options = options;
	menu->close_result = MENU_CLOSE_RESULT_NONE;
	menu->font = font;
	menu->head = NULL;
	menu->tail = NULL;
	menu->selected = NULL;
	menu->updated = 0;
	menu->default_color = color_white;
	menu->selected_color = color_red;
	menu->width = 0;
	menu->height = 0;
	menu->last_ts = 0;
	menu->threshold = 500 * 1000; /* 500 ms */

	return menu;
}

void
menu_free (Menu * menu)
{
	MenuEntry *e;

	for (e = menu->head; e != NULL; e = e->next)
		menu_entry_free (e);
}

void
menu_set_default_color (Menu * menu, const SDL_Color * color)
{
	menu->default_color = *color;
	menu->updated = 1;
}

void
menu_set_selected_color (Menu * menu, const SDL_Color * color)
{
	menu->selected_color = *color;
	menu->updated = 1;
}

int
menu_get_width (Menu * menu)
{
	return menu->width;
}

int
menu_get_height (Menu * menu)
{
	return menu->height;
}

int
menu_get_selected_id (Menu * menu)
{
	if (menu->selected == NULL)
		return -1;

	return menu->selected->id;
}

int
menu_get_close_result (Menu * menu)
{
	return menu->close_result;
}

MenuState
menu_update (Menu * menu)
{
	MenuState state = MENU_STATE_VISIBLE;
	MenuEntry *entry;
	SceCtrlData pad;
	SceCtrlLatch latch;

	menu->close_result = MENU_CLOSE_RESULT_NONE;

	sceCtrlReadBufferPositive (&pad, 1);
	sceCtrlReadLatch (&latch);

	if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_UP))
		menu_select_prev_entry (menu);

	if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_DOWN))
		menu_select_next_entry (menu);

	entry = menu->selected;

	switch (entry->type) {
		case MENU_ENTRY_TYPE_BUTTON:
			if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_CROSS)) {
				state = MENU_STATE_CLOSE;
				menu->close_result = MENU_CLOSE_RESULT_BUTTON;
			}
			break;

		case MENU_ENTRY_TYPE_SWITCH:
			if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_LEFT) ||
					EVENT_BUTTON_DOWN (&latch, PSP_CTRL_RIGHT)) {
				menu_switch_entry_toggle ((MenuSwitchEntry *) entry);

				/* re-render to reflect change */
				menu_entry_render (entry, menu->font,
						&menu->selected_color);
			}
			break;

		case MENU_ENTRY_TYPE_SCALE:
		{
			MenuScaleEntry *scale = (MenuScaleEntry *) entry;
			int val = menu_scale_entry_get_value (scale);

			if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_LEFT)) {
				menu_scale_entry_set_value (scale, val - 1);
				menu_repeat_button_reset (menu, &pad);
			} else if (menu_is_button_repeated (menu, &pad,
						PSP_CTRL_LEFT)) {
				menu_scale_entry_set_value (scale, val - 1);
			} else if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_RIGHT)) {
				menu_scale_entry_set_value (scale, val + 1);
				menu_repeat_button_reset (menu, &pad);
			} else if (menu_is_button_repeated (menu, &pad,
						PSP_CTRL_RIGHT)) {
				menu_scale_entry_set_value (scale, val + 1);
			}

			menu_entry_render (entry, menu->font,
					&menu->selected_color);

			break;
		}

		case MENU_ENTRY_TYPE_COMBOBOX:
			if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_LEFT))
				menu_combo_box_entry_prev ((MenuComboBoxEntry *) entry);
			else if (EVENT_BUTTON_DOWN (&latch, PSP_CTRL_RIGHT))
				menu_combo_box_entry_next ((MenuComboBoxEntry *) entry);

			menu_entry_render (entry, menu->font,
					&menu->selected_color);
			break;


		case MENU_ENTRY_TYPE_LABEL:
		default:
			break;

	}

	if ((menu->options & MENU_CANCEL_ON_START) &&
			EVENT_BUTTON_DOWN (&latch, PSP_CTRL_START))
		state = MENU_STATE_CANCELLED;
	if ((menu->options & MENU_BACK_ON_CIRCLE) &&
			EVENT_BUTTON_DOWN (&latch, PSP_CTRL_CIRCLE)) {
		state = MENU_STATE_CLOSE;
		menu->close_result = MENU_CLOSE_RESULT_BACK;
	}

	return state;
}

void
menu_render_to (Menu * menu, SDL_Surface * dest, const SDL_Rect * position)
{
	MenuEntry *e;
	SDL_Rect dest_pos = { 0, 0, 0, 0 };

	dest_pos.x = position->x;
	dest_pos.y = position->y;

	if (menu->updated) {
		menu_refresh_all_entries (menu);
		menu->updated = 0;
	}

	for (e = menu->head; e != NULL; e = e->next) {
		if (e->surface == NULL)
			continue;

		PSPLOG_DEBUG ("menu: blitting entry %p surface (%s) @ (%d,%d)",
				e, e->title, dest_pos.x, dest_pos.y);

		SDL_BlitSurface (e->surface, NULL, dest, &dest_pos);

		/* update position for next entry */
		dest_pos.y += dest_pos.h;
	}
}

int
menu_select_entry (Menu * menu, MenuEntry * entry)
{
	if (entry->owner != menu)
		return -1;

	return menu_select_entry_helper (menu, entry);
}

int
menu_select_prev_entry (Menu * menu)
{
	if (menu->selected == NULL)
		return -1;

	if (menu->selected->prev == NULL)
		return 0;

	return menu_select_entry_helper (menu, menu->selected->prev);
}

int
menu_select_next_entry (Menu * menu)
{
	if (menu->selected == NULL)
		return -1;

	if (menu->selected->next == NULL)
		return 0;

	return menu_select_entry_helper (menu, menu->selected->next);
}

int
menu_add_entry (Menu * menu, MenuEntry * entry)
{
	SDL_Color *color;

	/* first entry will be selected by default */
	if (menu->head == NULL)
		color = &menu->selected_color;
	else
		color = &menu->default_color;

	if (menu_entry_render (entry, menu->font, color) < 0)
		goto render_failed;

	if (((unsigned int) entry->surface->w) > menu->width)
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

	entry->owner = menu;

	return 0;

render_failed:
	PSPLOG_ERROR ("failed to render menu entry");
	return -1;
}

void
menu_remove_entry (Menu * menu, MenuEntry * entry)
{
	if (entry->owner != menu)
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

/**
 * MenuEntry implementation
 */
static int
menu_entry_render_default (MenuEntry * entry, TTF_Font * font,
		const SDL_Color * color)
{
	SDL_Surface *surface;

	surface = TTF_RenderText_Blended (font, entry->title, *color);
	if (!surface)
		return -1;

	menu_surface_replace_helper (&entry->surface, surface);
	return 0;
}

static void
menu_entry_init (MenuEntry * entry, MenuEntryType type, int id,
		const char * title, MenuEntryFreeFunction free)
{
	entry->type = type;
	entry->owner = NULL;
	entry->id = id;
	entry->title = strdup(title);
	entry->surface = NULL;
	entry->prev = entry->next = NULL;
	entry->free = free;
	entry->render = menu_entry_render_default;
}

static int
menu_entry_render(MenuEntry * entry, TTF_Font * font, const SDL_Color * color)
{
	if (!entry->render)
		return -1;

	return entry->render (entry, font, color);
}

void
menu_entry_free (MenuEntry * entry)
{
	if (entry->free)
		entry->free (entry);

	if (entry->surface)
		SDL_FreeSurface (entry->surface);

	if (entry->title)
		free (entry->title);

	free (entry);
}

MenuEntryType
menu_entry_get_type (MenuEntry * entry)
{
	return entry->type;
}

/**
 * MenuLabelEntry API implementation
 */
MenuLabelEntry *
menu_label_entry_new (int id, const char * label)
{
	MenuLabelEntry *entry;

	entry = malloc(sizeof(MenuButtonEntry));
	if (!entry)
		return NULL;

	menu_entry_init (&entry->parent, MENU_ENTRY_TYPE_LABEL, id, label,
			NULL);

	return entry;
}

/**
 * MenuButtonEntry API implementation
 */
MenuButtonEntry *
menu_button_entry_new (int id, const char * title)
{
	MenuButtonEntry *entry;

	entry = malloc(sizeof(MenuButtonEntry));
	if (!entry)
		return NULL;

	menu_entry_init (&entry->parent, MENU_ENTRY_TYPE_BUTTON, id, title,
			NULL);

	return entry;
}

/**
 * MenuSwitchEntry API implementation
 */
static int
menu_switch_entry_render (MenuEntry * entry, TTF_Font * font,
		const SDL_Color * color)
{
	MenuSwitchEntry *sw_entry = (MenuSwitchEntry *) entry;
	SDL_Surface *surface;
	const char *value_str;
	char *text;
	int len;
	int ret = -1;

	if (sw_entry->active)
		value_str = sw_entry->on_label;
	else
		value_str = sw_entry->off_label;

	len = 10 + strlen (entry->title) + strlen (value_str);
	text = malloc (len * sizeof(char));
	snprintf (text, len, "%s : <- %s ->", entry->title, value_str);

	surface = TTF_RenderText_Blended (font, text, *color);
	if (!surface) {
		PSPLOG_ERROR ("failed to render switch text, reason: %s",
				TTF_GetError ());
		goto done;
	}

	menu_surface_replace_helper (&entry->surface, surface);
	ret = 0;

done:
	free (text);
	return ret;
}

static void
menu_switch_entry_free (MenuSwitchEntry * entry)
{
	if (entry->surface_off)
		SDL_FreeSurface (entry->surface_off);

	if (entry->surface_on)
		SDL_FreeSurface (entry->surface_on);

	free (entry->on_label);
	free (entry->off_label);
}

MenuSwitchEntry *
menu_switch_entry_new (int id, const char *title)
{
	MenuSwitchEntry *entry;

	entry = malloc (sizeof(MenuSwitchEntry));

	menu_entry_init (&entry->parent, MENU_ENTRY_TYPE_SWITCH, id, title,
			(MenuEntryFreeFunction) menu_switch_entry_free);
	entry->on_label = strdup("on");
	entry->off_label = strdup("off");
	entry->surface_on = NULL;
	entry->surface_off = NULL;
	entry->active = 0;
	entry->toggled = NULL;

	entry->parent.render = menu_switch_entry_render;

	return entry;
}

int
menu_switch_entry_get_active (MenuSwitchEntry * entry)
{
	return entry->active;
}

void
menu_switch_entry_set_active (MenuSwitchEntry * entry, int is_active)
{
	if (entry->active == is_active)
		return;

	entry->active = is_active;
	if (entry->toggled)
		entry->toggled (entry, entry->userdata);
}

void
menu_switch_entry_toggle (MenuSwitchEntry * entry)
{
	menu_switch_entry_set_active (entry, !entry->active);
}

void
menu_switch_entry_set_values_labels (MenuSwitchEntry * entry,
		const char * off_label, const char * on_label)
{
	if (off_label) {
		free (entry->off_label);
		entry->off_label = strdup (off_label);
	}

	if (on_label) {
		free (entry->on_label);
		entry->on_label = strdup (on_label);
	}
}

void
menu_switch_entry_set_toggled_callback (MenuSwitchEntry * entry,
		MenuSwitchEntryToggledCallback callback, void * userdata)
{
	entry->toggled = callback;
	entry->userdata = userdata;
}

/**
 * MenuScaleEntry API implementation
 */
static int
menu_scale_entry_render (MenuEntry * entry, TTF_Font * font,
		const SDL_Color * color)
{
	MenuScaleEntry *scale_entry = (MenuScaleEntry *) entry;
	SDL_Surface *surface;
	char *text;
	int len = 255;
	int ret = -1;

	text = malloc (len * sizeof(char));
	snprintf (text, len, "%s : <- %d ->", entry->title,
			scale_entry->current);

	surface = TTF_RenderText_Blended (font, text, *color);
	if (!surface) {
		PSPLOG_ERROR ("failed to render scale button, reason: %s",
				TTF_GetError ());
		goto done;
	}

	menu_surface_replace_helper (&entry->surface, surface);
	ret = 0;

done:
	free (text);
	return ret;
}

MenuScaleEntry *
menu_scale_entry_new (int id, const char *title, int min, int max)
{
	MenuScaleEntry *entry;

	if (max < min)
		return NULL;

	entry = malloc (sizeof (MenuScaleEntry));

	menu_entry_init (&entry->parent, MENU_ENTRY_TYPE_SCALE, id, title, NULL);

	entry->min = min;
	entry->max = max;
	entry->current = min;

	entry->userdata = NULL;
	entry->value_changed = NULL;

	entry->parent.render = menu_scale_entry_render;

	return entry;
}

int
menu_scale_entry_get_value (MenuScaleEntry * entry)
{
	return entry->current;
}

void
menu_scale_entry_set_value (MenuScaleEntry * entry, int value)
{
	if (value < entry->min)
		value = entry->min;

	if (value > entry->max)
		value = entry->max;

	entry->current = value;

	if (entry->value_changed)
		entry->value_changed (entry, entry->userdata);
}

void
menu_scale_entry_set_value_changed_callback (MenuScaleEntry * entry,
		MenuScaleEntryValueChangedCallback callback, void * userdata)
{
	entry->value_changed = callback;
	entry->userdata = userdata;
}

/**
 * MenuComboBoxEntry API implementation
 */
static void
combo_box_item_free (ComboBoxItem * item)
{
	free (item->label);
	free (item);
}

static void
menu_combo_box_entry_free (MenuEntry * entry)
{
	MenuComboBoxEntry *combo_entry = (MenuComboBoxEntry *) entry;
	ComboBoxItem *item;

	item = combo_entry->items;
	while (item != NULL) {
		ComboBoxItem *tmp = item->next;
		combo_box_item_free (item);
		item = tmp;
	}
}

static int
menu_combo_box_entry_render (MenuEntry * entry, TTF_Font * font,
		const SDL_Color * color)
{
	MenuComboBoxEntry *combo_entry = (MenuComboBoxEntry *) entry;
	ComboBoxItem *item = combo_entry->current;
	SDL_Surface *surface;
	char *text;
	int len = 255;
	int ret = -1;

	text = malloc (len * sizeof(char));
	snprintf (text, len, "%s : <- %s ->", entry->title,
			item != NULL ? item->label : "(null)");

	surface = TTF_RenderText_Blended (font, text, *color);
	if (!surface) {
		PSPLOG_ERROR ("failed to render combo box, reason: %s",
				TTF_GetError ());
		goto done;
	}

	menu_surface_replace_helper (&entry->surface, surface);
	ret = 0;

done:
	free (text);
	return ret;
}

MenuComboBoxEntry *
menu_combo_box_entry_new (int id, const char *title)
{
	MenuComboBoxEntry *entry;

	entry = malloc (sizeof (MenuComboBoxEntry));

	menu_entry_init (&entry->parent, MENU_ENTRY_TYPE_COMBOBOX, id, title,
			menu_combo_box_entry_free);

	entry->items = NULL;
	entry->current = NULL;

	entry->parent.render = menu_combo_box_entry_render;

	return entry;
}

int
menu_combo_box_entry_get_value (MenuComboBoxEntry * entry)
{
	if (entry->current)
		return entry->current->id;
	else
		return -1;
}

int
menu_combo_box_entry_set_value (MenuComboBoxEntry * entry, int id)
{
	ComboBoxItem *item = entry->items;

	while (item) {
		if (item->id == id) {
			entry->current = item;
			return 0;
		}
		item = item->next;
	}

	return -1;
}

int
menu_combo_box_entry_append (MenuComboBoxEntry * entry, int id,
		const char * label)
{
	ComboBoxItem *item;
	ComboBoxItem *cur = entry->items;

	item = malloc (sizeof (*item));
	item->id = id;
	item->label = strdup (label);
	item->prev = item->next = NULL;

	if (cur == NULL) {
		/* no item in combo yet */
		entry->items = item;
		entry->current = item;
		return 0;
	}

	while (cur->next != NULL)
		cur = cur->next;

	cur->next = item;
	item->prev = cur;
	return 0;
}

void
menu_combo_box_entry_next (MenuComboBoxEntry * entry)
{
	if (entry->current && entry->current->next)
		entry->current = entry->current->next;
}

void
menu_combo_box_entry_prev (MenuComboBoxEntry * entry)
{
	if (entry->current && entry->current->prev)
		entry->current = entry->current->prev;
}

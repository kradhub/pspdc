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

#ifndef MENU_H
#define MENU_H

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#define MENU_CANCEL_ON_START 1

typedef enum
{
	MENU_STATE_CLOSE = 0,
	MENU_STATE_VISIBLE,
	MENU_STATE_CANCELLED,
} MenuState;

typedef enum
{
	MENU_ENTRY_TYPE_BASE = 0,
	MENU_ENTRY_TYPE_LABEL,
	MENU_ENTRY_TYPE_BUTTON,
	MENU_ENTRY_TYPE_SWITCH
} MenuEntryType;

typedef struct _Menu Menu;
typedef struct _MenuEntry MenuEntry;
typedef struct _MenuLabelEntry MenuLabelEntry;
typedef struct _MenuButtonEntry MenuButtonEntry;
typedef struct _MenuSwitchEntry MenuSwitchEntry;

typedef void (*MenuSwitchEntryToggledCallback)(MenuSwitchEntry * entry, void * userdata);

Menu *menu_new (TTF_Font * font, int options);
void menu_free (Menu * menu);

void menu_set_default_color (Menu * menu, const SDL_Color * color);
void menu_set_selected_color (Menu * menu, const SDL_Color * color);

int menu_get_width (Menu * menu);
int menu_get_height (Menu * menu);
int menu_get_selected_id (Menu * menu);

int menu_add_entry (Menu * menu, MenuEntry * entry);
void menu_remove_entry (Menu * menu, MenuEntry * entry);

int menu_select_entry (Menu * menu, MenuEntry * entry);
int menu_select_prev_entry (Menu * menu);
int menu_select_next_entry (Menu * menu);

MenuState menu_update (Menu * menu);
void menu_render_to (Menu * menu, SDL_Surface * dest, const SDL_Rect * position);

/* MenuEntry API */
void menu_entry_free (MenuEntry * entry);
MenuEntryType menu_entry_get_type (MenuEntry * entry);

/* MenuLabelEntry API */
MenuLabelEntry *menu_label_entry_new (int id, const char * label);

/* MenuButtonEntry API */
MenuButtonEntry *menu_button_entry_new (int id, const char * title);

/* MenuSwitchEntry API */
MenuSwitchEntry *menu_switch_entry_new (int id, const char * title);
int menu_switch_entry_get_active (MenuSwitchEntry * entry);
void menu_switch_entry_set_active (MenuSwitchEntry * entry, int is_active);
void menu_switch_entry_toggle (MenuSwitchEntry * entry);
void menu_switch_entry_set_values_labels (MenuSwitchEntry * entry,
		const char * off_label, const char * on_label);
void menu_switch_entry_set_toggled_callback (MenuSwitchEntry * entry,
		MenuSwitchEntryToggledCallback callback, void * userdata);

#endif

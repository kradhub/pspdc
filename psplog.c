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

#include <stdio.h>
#include <stdarg.h>
#include <pspdebug.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>

#include "psplog.h"

#define BUFFER_LEN 512

#define COLOR_RED    0x0000ffU
#define COLOR_GREEN  0x00ff00U
#define COLOR_BLUE   0xff0000U
#define COLOR_YELLOW 0x00ffffU
#define COLOR_WHITE  0xffffffU

enum psplog_output
{
	PSPLOG_OUTPUT_SCREEN,
	PSPLOG_OUTPUT_FILE,
};

static enum psplog_category level_threshold;
static enum psplog_output output;
static SceUID psplog_semaphore = 0;
static SceUID psplog_fd = -1;

static const char * const psplog_category_str[] = {
	"ERROR",
	"WARN",
	"INFO",
	"DEBUG"
};

static const char *
psplog_category_get_name (enum psplog_category cat)
{
	if (cat >= PSPLOG_CAT_ERROR || cat <= PSPLOG_CAT_DEBUG)
		return psplog_category_str[cat];
	else
		return "?????";
}

int
psplog_init (enum psplog_category level, const char *path)
{
	output = path ? PSPLOG_OUTPUT_FILE : PSPLOG_OUTPUT_SCREEN;

	if (output == PSPLOG_OUTPUT_FILE) {
		psplog_fd = sceIoOpen (path,
				PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		if (psplog_fd < 0)
			return -1;
	} else {
		pspDebugScreenInit ();
		pspDebugScreenSetXY (0, 0);
	}

	psplog_semaphore = sceKernelCreateSema ("psplog_semaphore", 0, 1, 1,
			NULL);

	level_threshold = level;

	return 0;
}

void
psplog_deinit()
{
	if (psplog_fd > 0)
		sceIoClose (psplog_fd);

	sceKernelDeleteSema (psplog_semaphore);
}

int
psplog_format (char *buf, size_t size, enum psplog_category cat,
		const char * fmt, va_list ap)
{
	int wbytes;

	/* buffer size should be greater than 9 to contains at least category
	 * and one character + \n */
	if (size < 9)
		return -1;

	wbytes = snprintf (buf, size, "%s ", psplog_category_get_name(cat));

	/* keep 1 byte of buf for \n */
	wbytes += vsnprintf (buf + wbytes, size - wbytes - 1, fmt, ap);
	buf[wbytes++] = '\n';
	return wbytes;
}

int
psplog_print_file (enum psplog_category cat, const char *buf, size_t size)
{
	int wbytes;

	if (psplog_fd > 0) {
		wbytes = sceIoWrite (psplog_fd, buf, size);
		/* sceIoSync("ms0:", 0); */
		return wbytes;
	}

	return -1;
}

int
psplog_print_screen (enum psplog_category cat, const char *buf, size_t size)
{
	u32 color;

	switch (cat) {
		case PSPLOG_CAT_ERROR:
			color = COLOR_RED;
			break;
		case PSPLOG_CAT_WARNING:
			color = COLOR_YELLOW;
			break;
		case PSPLOG_CAT_INFO:
			color = COLOR_WHITE;
			break;
		case PSPLOG_CAT_DEBUG:
			color = COLOR_BLUE;
			break;
		default:
			color = COLOR_WHITE;
			break;
	}

	pspDebugScreenSetTextColor (color);
	pspDebugScreenPrintData (buf, size);
	pspDebugScreenSetTextColor (COLOR_WHITE);

	return 0;
}

void
psplog_print (enum psplog_category cat, const char * fmt, ...)
{
	va_list ap;
	char buf[BUFFER_LEN] = { 0, };
	int size;

	if (cat > level_threshold)
		return;

	if (sceKernelWaitSema (psplog_semaphore, 1, NULL) < 0)
		goto done;

	va_start (ap, fmt);
	size = psplog_format (buf, BUFFER_LEN, cat, fmt, ap);
	va_end (ap);

	if (output == PSPLOG_OUTPUT_FILE) {
		psplog_print_file (cat, buf, size);
	} else {
		psplog_print_screen (cat, buf, size);
	}

	sceKernelSignalSema (psplog_semaphore, 1);

done:
	return;
}

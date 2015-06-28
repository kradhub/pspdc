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

#ifndef PSPLOG_H_
#define PSPLOG_H_

enum psplog_category {
	PSPLOG_CAT_ERROR = 0,
	PSPLOG_CAT_WARNING,
	PSPLOG_CAT_INFO,
	PSPLOG_CAT_DEBUG
};

#define PSPLOG_ERROR(fmt, ...) \
	psplog_print (PSPLOG_CAT_ERROR, (fmt), ##__VA_ARGS__)
#define PSPLOG_WARNING(fmt, ...) \
	psplog_print (PSPLOG_CAT_WARNING, (fmt), ##__VA_ARGS__)
#define PSPLOG_INFO(fmt, ...) \
	psplog_print (PSPLOG_CAT_INFO, (fmt), ##__VA_ARGS__)
#define PSPLOG_DEBUG(fmt, ...) \
	psplog_print (PSPLOG_CAT_DEBUG, (fmt), ##__VA_ARGS__)

/**
 * Init psplog context
 *
 * @path : path to log file. It could be NULL, in this case log output on screen
 */
int psplog_init(const char *path);

void psplog_deinit();

void psplog_print(enum psplog_category cat, const char * fmt, ...);


#endif

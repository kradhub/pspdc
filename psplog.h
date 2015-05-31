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

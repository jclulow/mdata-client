
#include <stdlib.h>
#include <err.h>
#include <string.h>

#include "dynstr.h"

typedef struct string {
	size_t str_strlen;
	size_t str_datalen;
	char *str_data;
} string_t;

#define	STRING_CHUNK_SIZE	64

void
dynstr_reset(string_t *str)
{
	if (str->str_data == NULL)
		return;

	str->str_strlen = 0;
	str->str_data[0] = '\0';
}

size_t
dynstr_len(string_t *str)
{
	return (str->str_strlen);
}

const char *
dynstr_cstr(string_t *str)
{
	return (str->str_data);
}

void
dynstr_append(string_t *str, const char *news)
{
	int len = strlen(news);
	int chunksz = STRING_CHUNK_SIZE;

	while (chunksz < len)
		chunksz *= 2;

	if (len + str->str_strlen >= str->str_datalen) {
		str->str_datalen += chunksz;
		str->str_data = realloc(str->str_data, str->str_datalen);
		if (str->str_data == NULL)
			err(1, "could not allocate memory for string");
	}
	strcpy(str->str_data + str->str_strlen, news);
	str->str_strlen += len;
}

string_t *
dynstr_new(void)
{
	string_t *ret = calloc(1, sizeof (string_t));

	if (ret == NULL)
		err(10, "could not allocate memory for string");

	return (ret);
}
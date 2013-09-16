
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <strings.h>

#include "dynstr.h"
#include "plat.h"

typedef enum mdata_exit_codes {
	MDEC_SUCCESS = 0,
	MDEC_NOTFOUND = 1,
	MDEC_ERROR = 2,
	MDEC_USAGE_ERROR = 3,
	MDEC_TRY_AGAIN = 10
} mdata_exit_codes_t;

typedef enum mdata_get_response {
	MDGR_UNKNOWN = 1,
	MDGR_NOTFOUND,
	MDGR_SUCCESS
} mdata_get_response_t;

typedef enum mdata_get_state {
	MDGS_MESSAGE_HEADER = 1,
	MDGS_MESSAGE_DATA,
	MDGS_DONE
} mdata_get_state_t;

typedef struct mdata_get {
	FILE *mdg_fp;
	char *mdg_keyname;
	string_t *mdg_data;
	mdata_get_state_t mdg_state;
	mdata_get_response_t mdg_response;
} mdata_get_t;

void
process_input(mdata_get_t *mdg, const char *buf)
{
	switch (mdg->mdg_state) {
	case MDGS_MESSAGE_HEADER:
		if (strcmp(buf, "NOTFOUND") == 0) {
			mdg->mdg_response = MDGR_NOTFOUND;
			mdg->mdg_state = MDGS_DONE;
		} else if (strcmp(buf, "SUCCESS") == 0) {
			mdg->mdg_response = MDGR_SUCCESS;
			mdg->mdg_state = MDGS_MESSAGE_DATA;
		} else {
			dynstr_append(mdg->mdg_data, buf);
			mdg->mdg_response = MDGR_UNKNOWN;
			mdg->mdg_state = MDGS_DONE;
		}
		break;
	case MDGS_MESSAGE_DATA:
		if (strcmp(buf, ".") == 0) {
			mdg->mdg_state = MDGS_DONE;
		} else {
			int offs = buf[0] == '.' ? 1 : 0;
			if (dynstr_len(mdg->mdg_data) > 0)
				dynstr_append(mdg->mdg_data, "\n");
			dynstr_append(mdg->mdg_data, buf + offs);
		}
		break;
	case MDGS_DONE:
		break;
	default:
		abort();
	}
}

void
write_get(mdata_get_t *mdg)
{
	char *x;
	int len;
	int actual;

	if ((len = asprintf(&x, "GET %s\n", mdg->mdg_keyname)) < 0) {
		abort();
	}

	if ((actual = fwrite(x, 1, len, mdg->mdg_fp)) != len) {
		err(12, "could not write thing");
	}

	/*
	 * Wait for response header from remote peer:
	 */
	mdg->mdg_state = MDGS_MESSAGE_HEADER;

	free(x);
}

void
read_response(mdata_get_t *mdg)
{
	int retries = 3;
	string_t *resp = dynstr_new();

	for (;;) {
		char buf[2];
		ssize_t sz = fread(&buf, 1, 1, mdg->mdg_fp);

		if (sz == 1) {
			if (buf[0] == '\n') {
				process_input(mdg, dynstr_cstr(resp));
				dynstr_reset(resp);
			} else {
				buf[1] = '\0';
				dynstr_append(resp, buf);
			}
		} else if ((sz == 0) || (sz == -1 && errno == EAGAIN)) {
			if (--retries == 0)
				errx(1, "timed out while reading metadata "
				    "response");
			sleep(1);
		} else {
			errx(1, "could not read metadata response");
		}

		if (mdg->mdg_state == MDGS_DONE)
			break;
	}
}

void
print_response(mdata_get_t *mdg)
{
	switch (mdg->mdg_response) {
	case MDGR_SUCCESS:
		fprintf(stdout, "%s\n", dynstr_cstr(mdg->mdg_data));
		break;
	case MDGR_NOTFOUND:
		fprintf(stderr, "No metadata for '%s'\n", mdg->mdg_keyname);
		break;
	case MDGR_UNKNOWN:
		fprintf(stderr, "Error getting metadata for key '%s': %s\n",
		    mdg->mdg_keyname, dynstr_cstr(mdg->mdg_data));
		break;
	default:
		abort();
	}
}

int
main(int argc, char **argv)
{
	char *errmsg;
	mdata_get_t mdg;

	if (argc < 2) {
		errx(MDEC_USAGE_ERROR, "Usage: %s <keyname>", argv[0]);
	}

	bzero(&mdg, sizeof (mdg));
	mdg.mdg_keyname = strdup(argv[1]);
	mdg.mdg_data = dynstr_new();

	if (open_metadata_stream(&mdg.mdg_fp, &errmsg) == -1) {
		errx(MDEC_TRY_AGAIN, "%s", errmsg);
	}

	write_get(&mdg);
	read_response(&mdg);
	print_response(&mdg);

	(void) fclose(mdg.mdg_fp);
	mdg.mdg_fp = NULL;
	free(mdg.mdg_keyname);

	if (mdg.mdg_state == MDGS_DONE) {
		if (mdg.mdg_response == MDGR_SUCCESS)
			return (MDEC_SUCCESS);
		else if (mdg.mdg_response == MDGR_NOTFOUND)
			return (MDEC_NOTFOUND);
		else
			return (MDEC_ERROR);
	} else {
		return (MDEC_TRY_AGAIN);
	}
}


#include <stdlib.h>
#include <err.h>
#include <smbios.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <termios.h>
#include <zone.h>
#include <sys/socket.h>
#include <sys/un.h>

#define	IN_ZONE_SOCKET		"/.zonecontrol/metadata.sock"
#define	IN_GLOBAL_DEVICE	"/dev/term/b"

static int
raw_mode(int fd, char **errmsg)
{
	struct termios tios;

	if (tcgetattr(fd, &tios) == -1) {
		*errmsg = "could not set raw mode on serial device";
		return (-1);
	}

	tios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	tios.c_oflag &= ~(OPOST);
	tios.c_cflag |= (CS8);
	tios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	tios.c_cc[VMIN] = 0;
	tios.c_cc[VTIME] = 1;

	if (tcsetattr(fd, TCSAFLUSH, &tios) == -1) {
		*errmsg = "could not get attributes from serial device";
		return (-1);
	}

	return (0);
}

static int
find_product(smbios_hdl_t *shp, const smbios_struct_t *sp, void *arg)
{
	char **outputp = arg;
	smbios_info_t info;

	if (sp->smbstr_type != SMB_TYPE_SYSTEM)
		return (0);

	if (smbios_info_common(shp, sp->smbstr_id, &info) != 0)
		return (0);

	if (info.smbi_product[0] != '\0') {
		*outputp = strdup(info.smbi_product);
	}

	return (0);
}

static char *
get_product_string(void)
{
	char *output = NULL;
	int e;
	smbios_hdl_t *shp;

	if ((shp = smbios_open(NULL, SMB_VERSION, 0, &e)) == NULL) {
		return (NULL);
	}

	smbios_iter(shp, find_product, &output);

	smbios_close(shp);

	return (output);
}

static int
open_md_ngz(int *outfd, char **errmsg)
{
	/*
	 * We're in a non-global zone, so try and connect to the
	 * metadata socket:
	 */
	int fd;
	struct sockaddr_un ua;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		*errmsg = "Could not open metadata socket.";
		return (-1);
	}

	bzero(&ua, sizeof (ua));
	ua.sun_family = AF_UNIX;
	strcpy(ua.sun_path, IN_ZONE_SOCKET);

	if (connect(fd, (struct sockaddr *)&ua, sizeof (ua)) == -1) {
		(void) close(fd);
		*errmsg = "Could not connect metadata socket.";
		return (-1);
	}

	*outfd = fd;

	return (0);
}

static int
open_md_gz(int *outfd, char **errmsg)
{
	/*
	 * We're in a global zone in a SmartOS KVM/QEMU instance, so
	 * try to use /dev/term/b for metadata.
	 */
	int fd;

	if ((fd = open(IN_GLOBAL_DEVICE, O_RDWR | O_EXCL |
	    O_NOCTTY)) == -1) {
		*errmsg = "Could not open serial device.";
		return (-1);
	}

	if (raw_mode(fd, errmsg) == -1) {
		(void) close(fd);
		return (-1);
	}

	*outfd = fd;

	return (0);
}

int
open_metadata_stream(FILE **outfp, char **errmsg)
{
	int fd;
	FILE *fp;
	char *product;
	boolean_t smartdc_hvm_guest = B_FALSE;

	if (getzoneid() != GLOBAL_ZONEID) {
		if (open_md_ngz(&fd, errmsg) != 0) {
			return (-1);
		}
		goto wrapfd;
	}

	/*
	 * Interrogate the SMBIOS data from the system to see if we're
	 * in a KVM/QEMU virtual machine:
	 */
	product = get_product_string();
	if (product != NULL && strcmp(product, "SmartDC HVM") == 0)
		smartdc_hvm_guest = B_TRUE;
	free(product);

	if (smartdc_hvm_guest) {
		if (open_md_gz(&fd, errmsg) != 0) {
			return (-1);
		}
		goto wrapfd;
	}

	/*
	 * We have no idea.
	 */
	*errmsg = "I don't know how to get metadata on this system.";
	return (-1);

wrapfd:
	fp = fdopen(fd, "r+");
	if (fp == NULL) {
		(void) close(fd);
		*errmsg = "Could not fdopen.";
		return (-1);
	}
	/*
	 * Disable buffering on the connection:
	 */
	if (setvbuf(fp, NULL, _IONBF, 0) != 0) {
		*errmsg = "Could not setvbuf.";
		(void) fclose(fp);
		return (-1);
	}

	*outfp = fp;
	return (0);
}

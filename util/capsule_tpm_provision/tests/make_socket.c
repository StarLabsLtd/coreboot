/* SPDX-License-Identifier: GPL-2.0-only */

#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int socket_file;

	if (argc != 2 || strlen(argv[1]) >= sizeof(address.sun_path))
		return 1;
	memcpy(address.sun_path, argv[1], strlen(argv[1]) + 1);
	socket_file = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_file < 0)
		return 1;
	if (bind(socket_file, (const struct sockaddr *)&address,
		offsetof(struct sockaddr_un, sun_path) + strlen(argv[1]) + 1)) {
		close(socket_file);
		return 1;
	}
	return close(socket_file) != 0;
}

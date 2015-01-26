	/*
		This file is part of telegram-cli.

		Telegram-cli is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 2 of the License, or
		(at your option) any later version.

		Telegram-cli is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with this telegram-cli.  If not, see <http://www.gnu.org/licenses/>.

		Copyright Vitaly Valtman 2013-2015
	*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
//exit():
#include <stdlib.h>

//void lua_new_msg (struct tgl_message *M);

/*function on_msg_receive (msg)
if msg.date < now then
return
end
if msg.out then
return
end
		local socket = require("socket")
local ip = assert(socket.dns.toip("127.0.0.1"))
local udp = assert(socket.udp())
assert(udp:sendto('[' .. msg.from.print_name .. ']' .. msg.text, ip, 11000))
end*/

#define SOCKET_ANSWER_MAX_SIZE (1 << 25)
static char socket_answer[SOCKET_ANSWER_MAX_SIZE + 1] = "hey.";

//packet size
int socked_fd;

#define DEF_PORT 4242
#define DEF_ADRR "127.0.0.1"

int main(int argc, char *argv[])
{
	//char ip[9] = "127.0.0.1";
	socked_fd = socket(AF_INET, SOCK_STREAM, 0); //lets do UDP Socket and listener_d is the Descriptor
	if (socked_fd == -1)
	{
		printf("I can't open the socket!");
		exit(2);
	}
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET; //still TCP
	serv_addr.sin_port = (in_port_t) htons((uint16_t) DEF_PORT); //The evil port.
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IPv4 local host address.

	int connection = connect(socked_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if (connection == -1) {
		printf("I can't speek!");
		exit(6);
	}

	/*if (inet_aton(ip, &si_other.sin_addr) == 0)
	{
		printf(stderr, "inet_aton() failed. (could not convert ip to binary address format)");
		exit(4);
	}*/
	/*for (i = 0; i < NPACK; i++)
	{
		printf("Sending packet %d\n", i);
		sprintf(buf, "This is packet %d\n", i);
		if (sendto(s, socket_answer, SOCKET_ANSWER_MAX_SIZE, 0, &si_other, slen) == -1)
		{
			printf("Error sending.");
			exit(5);
		}
	}*/
	//sendto(socked_fd,"hey",strlen(socket_answer),0, (struct sockaddr *)&serv_addr,sizeof(serv_addr));
	//Send some data
	if( send(socked_fd , &socket_answer , SOCKET_ANSWER_MAX_SIZE , 0) < 0)
	{
		puts("Send failed");
		return 1;
	}
	close(socked_fd);
	return 0;
}
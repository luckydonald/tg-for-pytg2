/*
	This file is partly part of telegram-cli.

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

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

//close():
#include <unistd.h>

//exit():
#include <stdlib.h>
#include "tgl/tgl-layout.h"
#include "tgl/tgl.h"

// while debug:
//#include "tgl/tgl.h"

// va_list, va_start, va_arg, va_end
#include <stdarg.h>
#include <arpa/inet.h>
#include <assert.h>

// string length strnlen()
#include <string.h>


#define SOCKET_ANSWER_MAX_SIZE (1 << 25)
static char socket_answer[SOCKET_ANSWER_MAX_SIZE + 1];
static int socket_answer_pos = -1;

extern struct tgl_state *TLS;



//packet size
int socked_fd;
struct sockaddr_in serv_addr;

#define DEFAULT_PORT 4458

void socket_init (char *address_string);
void socket_send(char *message); //TODO?

void lua_init(const char *address_string);
void lua_new_msg (struct tgl_message *M);

void lua_init (const char *address_string) {
	char *address_string_copy = malloc(sizeof(char) * strnlen(address_string,23));
	strcpy(address_string_copy, address_string);
	socket_init(address_string_copy);
}
void push_message (struct tgl_message *M);
char* expand_escapes_alloc(const char* src);


static void socket_answer_add_printf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void socket_answer_add_printf (const char *format, ...) {
	if (socket_answer_pos < 0) { return; }
	va_list ap;
	va_start (ap, format);
	socket_answer_pos += vsnprintf (socket_answer + socket_answer_pos, SOCKET_ANSWER_MAX_SIZE - socket_answer_pos, format, ap);
	va_end (ap);
	if (socket_answer_pos > SOCKET_ANSWER_MAX_SIZE) { socket_answer_pos = -1; }
}


#define push(...) \
  socket_answer_add_printf (__VA_ARGS__)

void lua_new_msg (struct tgl_message *M) {
	//TODO has socket? else just return.
	//socket_answer_start ();
	printf("Sending Message...\n");
	socket_answer_pos = 0;
	push("{");
	push_message (M);
	push("}");
	if (socket_answer_pos > 0) {
		printf("Message length: %i\n", socket_answer_pos);
		if( send(socked_fd, &socket_answer, socket_answer_pos , 0) < 0)
		{
			puts("Send failed.");
		} else {
			puts("Did send.");
		}
	}
	socket_answer_pos = -1;
}

void socket_init (char *address_string) {
	if (address_string == NULL) {
		printf("No address and no port given.\n");
		exit(3);
	}
	char *port_pos = NULL;
	uint16_t port = DEFAULT_PORT;
	strtok(address_string, ":");
	port_pos = strtok(NULL, ":"); //why is it needed doubled?
	if (port_pos == NULL) {
		printf("Address: \"%s\", no port given, using port %i instead.\n" , address_string, port);
	} else {
		port = atoi (port_pos);
		printf("Address: \"%s\", IP: %i.\n", address_string,port);
	}

	socked_fd = socket(AF_INET, SOCK_STREAM, 0); //lets do UDP Socket and listener_d is the Descriptor
	if (socked_fd == -1)
	{
		printf("I can't open the socket!\n");
		exit(5);
	} else {
		printf("Socket opened.\n");
	}
	memset((void *) &serv_addr, 0, sizeof(struct sockaddr_in));
	serv_addr.sin_family = AF_INET; //still TCP
	inet_pton(AF_INET, address_string, &(serv_addr.sin_addr)); //copy the adress.
	serv_addr.sin_port = (in_port_t) htons((uint16_t)port);
	int connection = connect(socked_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if (connection == -1) {
		printf("I can't connect!\n");
		exit(6);
	}

}







char *format_peer_type (int x) {
	switch (x) {
		case TGL_PEER_USER:
			return "user";
		case TGL_PEER_CHAT:
			return "chat";
		case TGL_PEER_ENCR_CHAT:
			return "encr_chat";
		default:
			assert (0);
			return("Nope.avi");
	}
}
char *format_bool(int bool) {
	return (bool ? "true": "false");
}
char *format_string_or_null(char *str) {
	if (str == NULL) {
		return "";
	} else {
		return str;
	}
}

void push_peer (tgl_peer_id_t id, tgl_peer_t *P);

void push_user (tgl_peer_t *P) {
	push("\"first_name\":\"%s\", \"last_name\":\"%s\", \"real_first_name\": \"%s\", \"real_last_name\": \"%s\", \"phone\":\"%s\"%s",
			P->user.first_name, P->user.last_name, format_string_or_null(P->user.real_first_name), format_string_or_null(P->user.real_last_name),  format_string_or_null(P->user.phone), (P->user.access_hash ? ", \"access_hash\":1":""));
}
void push_chat (tgl_peer_t *P) {
	assert (P->chat.title);
	push("\"title\":\"%s\", \"members_num\":%i",
			P->chat.title, P->chat.users_num);
	if (P->chat.user_list) {
		push(", \"members\": [");
		int i;
		for (i = 0; i < P->chat.users_num; i++) {
			if (i != 0) {
				push(", ");
			}
			tgl_peer_id_t id = TGL_MK_USER (P->chat.user_list[i].user_id);
			push_peer (id, tgl_peer_get (TLS, id));

		}
		push("]");
	}
}
void push_encr_chat (tgl_peer_t *P) {
	push ("\"user\": ");
	push_peer (TGL_MK_USER (P->encr_chat.user_id), tgl_peer_get (TLS, TGL_MK_USER (P->encr_chat.user_id)));
}

void push_peer (tgl_peer_id_t id, tgl_peer_t *P) {
	push("{");
	push("\"id\":%i, \"type\":\"%s\", \"print_name\": \"", tgl_get_peer_id (id), format_peer_type (tgl_get_peer_type (id)));
	//Note: opend quote for print_name's value!
	if (!P || !(P->flags & FLAG_CREATED)) {
		switch (tgl_get_peer_type (id)) {
			case TGL_PEER_USER:
				push("user#%d", tgl_get_peer_id (id));
				break;
			case TGL_PEER_CHAT:
				push("chat#%d", tgl_get_peer_id (id));
				break;
			case TGL_PEER_ENCR_CHAT:
				push("encr_chat#%d", tgl_get_peer_id (id));
				break;
			default:
				assert (0);
		}
		push("\"}");
		return;

	}

	//P is defined did not return.

	push("%s\", ", P->print_name);

	switch (tgl_get_peer_type(id))
	{
		case TGL_PEER_USER:
			push_user(P);
			break;
		case TGL_PEER_CHAT:
			push_chat(P);
			break;
		case TGL_PEER_ENCR_CHAT:
			push_encr_chat(P);
			break;
		default:
			assert(0);
	}

	push(", \"flags\": %i}",  P->flags);
}
void push_geo(struct tgl_geo geo) {
	push("\"longitude\": %f, \"latitude\": %f", geo.longitude, geo.latitude);
}
void push_size(int size){
	push("\"size\":\"");
	if (size < (1 << 10)) {
		push("%dB", size);
	} else if (size < (1 << 20)) {
		push("%dKiB", size >> 10);
	} else if (size < (1 << 30)) {
		push("%dMiB", size >> 20);
	} else {
		push("%dGiB", size >> 30);
	}
	push("\", \"bytes\":%d", size);
}


void push_media (struct tgl_message_media *M) {
	push("{");
	switch (M->type) {
		case tgl_message_media_photo:
			push("\"type\": \"photo\", \"encrypted\": false");
			if (M->photo.caption && strlen (M->photo.caption))
			{
				push (", \"caption\":\"%s\"", expand_escapes_alloc(M->photo.caption));
			}
			break;
		case tgl_message_media_photo_encr:
			push("\"type\": \"photo\", \"encrypted\": true");
			break;
			/*case tgl_message_media_video:
			  case tgl_message_media_video_encr:
				lua_newtable (luaState);
				lua_add_string_field ("type", "video");
				break;
			  case tgl_message_media_audio:
			  case tgl_message_media_audio_encr:
				lua_newtable (luaState);
				lua_add_string_field ("type", "audio");
				break;*/
		case tgl_message_media_document:
			push("\"type\": \"document\", \"encrypted\": false, \"document\":\"");
			if (M->document.flags & FLAG_DOCUMENT_IMAGE) {
				push("image");
			} else if (M->document.flags & FLAG_DOCUMENT_AUDIO) {
				push("audio");
			} else if (M->document.flags & FLAG_DOCUMENT_VIDEO) {
				push("video");
			} else if (M->document.flags & FLAG_DOCUMENT_STICKER) {
				push("sticker");
			} else {
				push("document");
			}
			push("\""); //end of document's value.

			if (M->document.caption && strlen (M->document.caption)) {
				push(", \"caption\":\"%s\"", expand_escapes_alloc(M->document.caption));
			}

			if (M->document.mime_type) {
				push(", \"mime\":\"%s\"", M->document.mime_type);
			}

			if (M->document.w && M->document.h) {
				push(", \"dimension\":{\"width\":%d,\"height\":%d}", M->document.w, M->document.h);
			}

			if (M->document.duration) {
				push(", \"duration\":%d", M->document.duration);
			}
			push(", ");
			push_size(M->document.size);
			break;
		case tgl_message_media_document_encr:
			push("\"type\": \"document\", \"encrypted\": true, \"document\":\"");
			if (M->encr_document.flags & FLAG_DOCUMENT_IMAGE) {
				push("image");
			} else if (M->encr_document.flags & FLAG_DOCUMENT_AUDIO) {
				push("audio");
			} else if (M->encr_document.flags & FLAG_DOCUMENT_VIDEO) {
				push("video");
			} else if (M->encr_document.flags & FLAG_DOCUMENT_STICKER) {
				push("sticker");
			} else {
				push("document");
			}
			push("\""); //end of document's value.

			if (M->encr_document.caption && strlen (M->document.caption)) {
				push(", \"caption\":\"%s\"", expand_escapes_alloc(M->document.caption));
			}

			if (M->encr_document.mime_type) {
				push(", \"mime\":\"%s\"", M->document.mime_type);
			}

			if (M->encr_document.w && M->document.h) {
				push(", \"dimension\":{\"width\":%d,\"height\":%d}", M->document.w, M->document.h);
			}

			if (M->encr_document.duration) {
				push(", \"duration\":%d", M->encr_document.duration);
			}
			push(", ");
			push_size(M->encr_document.size);
			break;
		case tgl_message_media_unsupported:
			push("\"type\": \"unsupported\"");
			break;
		case tgl_message_media_geo:
			push("\"type\": \"geo\", ");
			push_geo(M->geo);
			push(", \"google\":\"https://maps.google.com/?q=%.6lf,%.6lf\"", M->geo.latitude, M->geo.longitude);
			break;
		case tgl_message_media_contact:
			push("\"type\": \"contact\", \"phone\": \"%s\", \"first_name\": \"%s\", \"last_name\": \"%s\", \"user_id\": %i",  M->phone, M->first_name, M->last_name, M->user_id);
			break;
		default:
			push("\"type\": \"\?\?\?\", \"typeid\":\"%d\"", M->type); //escaped "???" to avoid Trigraph. (see http://stackoverflow.com/a/1234618 )
			break;
	}
	push("}");
}

void push_message (struct tgl_message *M) {
	if (!(M->flags & FLAG_CREATED)) { return; }
	push("\"id\":%lld, \"flags\": %i, \"forward\":", M->id, M->flags);
	if (tgl_get_peer_type (M->fwd_from_id)) {
		push("{\"from\": ");
		push_peer (M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
		push(", \"date\": %i}", M->fwd_date);
	} else {
		push("null");
	}
	push(", \"from\":");
	push_peer (M->from_id, tgl_peer_get (TLS, M->from_id));
	push(", \"to\":");
	push_peer (M->to_id, tgl_peer_get (TLS, M->to_id));
	push(", \"out\": %s, \"unread\":%s, \"date\":%i, \"service\":%s", format_bool(M->out), format_bool(M->unread), M->date, format_bool(M->service) );

	if (!M->service) {
		if (M->message_len && M->message) {
			push(", \"text\": \"%s\"", expand_escapes_alloc(M->message)); // http://stackoverflow.com/a/3767300 //TODO: escape!
		}
		if (M->media.type && M->media.type != tgl_message_media_none) {
			push(", \"media\":");
			push_media (&M->media);
		}
	}
	// is no dict => no "}".
}

// http://stackoverflow.com/a/3535143
void expand_escapes(char* dest, const char* src)
{
	char c;

	while ((c = *(src++))) {
		switch(c) {
			case '\a':
				*(dest++) = '\\';
				*(dest++) = 'a';
				break;
			case '\b':
				*(dest++) = '\\';
				*(dest++) = 'b';
				break;
			case '\t':
				*(dest++) = '\\';
				*(dest++) = 't';
				break;
			case '\n':
				*(dest++) = '\\';
				*(dest++) = 'n';
				break;
			case '\v':
				*(dest++) = '\\';
				*(dest++) = 'v';
				break;
			case '\f':
				*(dest++) = '\\';
				*(dest++) = 'f';
				break;
			case '\r':
				*(dest++) = '\\';
				*(dest++) = 'r';
				break;
			case '\\':
				*(dest++) = '\\';
				*(dest++) = '\\';
				break;
			case '\"':
				*(dest++) = '\\';
				*(dest++) = '\"';
				break;
			case '\'':
				*(dest++) = '\\';
				*(dest++) = '\'';
				break;
			default:
				*(dest++) = c;
		}
	}

	*dest = '\0'; /* Ensure nul terminator */
}
/* Returned buffer may be up to twice as large as necessary */
char* expand_escapes_alloc(const char* src)
{
	char* dest = malloc(2 * strlen(src) + 1);
	expand_escapes(dest, src);
	return dest;
}

/*

void check () {
	closed = 1;
	while (1)
	{
		if (closed)
		{
			//Accepting a socket
			accepted_socket = accept_connection(accept_socket);

			//check if socket is valid
			if (accepted_socket <= 0)
			{
				printf("\t Error: Socket not valid, aborting\n");
				goto clean_up;
			}
			// print out a small message to indicate that a connection has been established
			printf("\tNew connection accepted\n");
			//marking connection as open
			closed = 0;
		}
		else
		{
			closed = handle_connection(accepted_socket);
			//if connection has been closed, print out a small message
			if (closed)
			{
				printf("\tConnection has been closed -> Waiting for new connections\n");
			}
		}
	}
}

*/

// Empty stuff:
void lua_secret_chat_update (struct tgl_secret_chat *C, unsigned flags) {
	return;
}
void lua_user_update (struct tgl_user *U, unsigned flags) {
	return;
}
void lua_diff_end (void) {
	return;
}
void lua_do_all (void) {
	return;
}
void lua_our_id (int id) {
	return;
}
void lua_binlog_end (void) {
	return;
}
void lua_chat_update (struct tgl_chat *C, unsigned flags) {
	return;
}

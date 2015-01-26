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
//exit():
#include <stdlib.h>
#include "tgl/tgl-layout.h"


#define SOCKET_ANSWER_MAX_SIZE (1 << 25)
static char socket_answer[SOCKET_ANSWER_MAX_SIZE + 1];
static int socket_answer_pos = -1;


//packet size
int socked_fd;
struct sockaddr_in serv_addr;

#define DEF_PORT 4242
#define DEF_ADRR "127.0.0.1"

void socket_init (const char *address_string);

void lua_init(const char *address_string);
void lua_new_msg (struct tgl_message *M);

int main(int argc, char *argv[])
{
	socket_init("127.0.0.1:4458");
	if( send(socked_fd , &socket_answer , SOCKET_ANSWER_MAX_SIZE , 0) < 0)
	{
		puts("Send failed");
		return 1;
	}
	close(socked_fd);
	return 0;
}

void lua_init (const char *address_string) {
	socket_init(address_string);
}
void lua_new_msg (struct tgl_message *M) {
	if (!have_file) { return; }
	push("{", NULL);
	push_message (M);
	push("}", NULL);
}

void socket_init (const char *address_string) {
	if (address_string == NULL) {
		printf("No address and no port given.\n");
		exit(3);
	}
	char *port_pos = NULL;
	uint16_t port = DEFAULT_PORT;
	strtok(address_string, ":");
	port_pos = strtok(NULL, ":"); //why is it needed doubled?
	if (port_pos == NULL) {
		printf("Address: '%s', no port given, using port %i instead.\n" , address_string, port);
	} else {
		port = atoi (port_pos);
		printf("Address: '%s', IP: %i.\n", address_string,port);
	}

	socked_fd = socket(AF_INET, SOCK_STREAM, 0); //lets do UDP Socket and listener_d is the Descriptor
	if (socked_fd == -1)
	{
		printf("I can't open the socket!");
		exit(2);
	}
	memset(serv_addr, 0, sizeof(struct sockaddr_in));
	serv_addr.sin_family = AF_INET; //still TCP
	serv_addr.sin_port = (in_port_t) htons((uint16_t) DEF_PORT); //The evil port.
	int connection = connect(socked_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	inet_pton(AF_INET, address_string, &(serv_addr.sin_addr)); //copy the adress.
	serv_addr.sin_port = (in_port_t) htons((uint16_t)port);
	if (connection == -1) {
		printf("I can't connect!");
		exit(6);
	}

}
void socket_send(char *message){
	if( send(socked_fd , &socket_answer , SOCKET_ANSWER_MAX_SIZE , 0) < 0)
	{
		puts("Send failed");
	}
}




void socket_answer_start (void) {
	socket_answer_pos = 0;
}

static void socket_answer_add_printf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void socket_answer_add_printf (const char *format, ...) {
	if (socket_answer_pos < 0) { return; }
	va_list ap;
	va_start (ap, format);
	socket_answer_pos += vsnprintf (socket_answer + socket_answer_pos, SOCKET_ANSWER_MAX_SIZE - socket_answer_pos, format, ap);
	va_end (ap);
	if (socket_answer_pos > SOCKET_ANSWER_MAX_SIZE) { socket_answer_pos = -1; }
}

void socket_answer_end (struct in_ev *ev) {
	if (ev->bev && socket_answer_pos > 0) {
		static char s[100];
		sprintf (s, "ANSWER %d\n", socket_answer_pos);
		bufferevent_write (ev->bev, s, strlen (s));
		bufferevent_write (ev->bev, socket_answer, socket_answer_pos);
		bufferevent_write (ev->bev, "\n", 1);
	}
	socket_answer_pos = -1;
}

#define push(...) \
  socket_answer_add_printf (__VA_ARGS__)







void push_peer (tgl_peer_id_t id, tgl_peer_t *P) {
	push("{");
	push("'id':%i, 'type':'%s', 'print_name': '", tgl_get_peer_id (id), format_peer_type (tgl_get_peer_type (id)));
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
		push("'}", NULL);
		return;

	}

	//P is defined did not return.

	push("%s', 'flags':%i", P->print_name, P->flags);

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

	push("', 'flags': %i}",  P->flags);
}
void push_user (tgl_peer_t *P) {
	push("'first_name':'%s', 'last_name':'%s', 'real_first_name': '%s', 'real_last_name': '%s', 'phone','%s'%s",
			P->user.first_name, P->user.last_name, P->user.real_first_name, P->user.real_last_name,  P->user.phone, (P->user.access_hash ? " 'access_hash':1":""));
}

void push_chat (tgl_peer_t *P) {
	assert (P->chat.title);
	push("'title':'%s', 'members_num':%i",
			P->chat.title, P->chat.users_num);
	if (P->chat.user_list) {
		push(", 'members': [", NULL);
		int i;
		for (i = 0; i < P->chat.users_num; i++) {
			if (i != 0) {
				push(", ", NULL);
			}
			tgl_peer_id_t id = TGL_MK_USER (P->chat.user_list[i].user_id);
			push_peer (id, tgl_peer_get (TLS, id));

		}
		push("]", NULL);
	}
}

void push_encr_chat (tgl_peer_t *P) {
	 push ("'user': ");
	push_peer (TGL_MK_USER (P->encr_chat.user_id), tgl_peer_get (TLS, TGL_MK_USER (P->encr_chat.user_id)));
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
	}
}
char *format_bool(void bool) {
	return (bool ? "true": "false");
}


void push_message (struct tgl_message *M) {
	if (!(M->flags & FLAG_CREATED)) { return; }
	push("'id':%lld, 'flags': %i, 'forward':", M->id, M->flags);
	if (tgl_get_peer_type (M->fwd_from_id)) {
		push("{'from': ", NULL);
		push_peer (M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
		push(", 'date': %i}", M->fwd_date);
	} else {
		push("null", NULL);
	}
	push(", 'from':", NULL);
	push_peer (M->from_id, tgl_peer_get (TLS, M->from_id));
	push(", 'to':", NULL);
	push_peer (M->to_id, tgl_peer_get (TLS, M->to_id));
	push(", 'out': %s, 'unread':%s, 'date':%i, 'service':%s", format_bool(M->out), format_bool(M->unread), M->date, format_bool(M->service), );

	if (!M->service) {
		if (M->message_len && M->message) {
			push(", 'text': '%.*s'", M->message_len, M->message); // http://stackoverflow.com/a/3767300 //TODO: escape!
		}
		if (M->media.type && M->media.type != tgl_message_media_none) {
			push(", 'media':", NULL);
			push_media (&M->media);
		}
	}
	push("}",NULL);
}

void push_media (struct tgl_message_media *M) {
	push("{", NULL);
	switch (M->type) {
		case tgl_message_media_photo:
		case tgl_message_media_photo_encr:
			push("'type': 'photo'", NULL);
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
		case tgl_message_media_document_encr:
			push("'type': 'document'", NULL);
			break;
		case tgl_message_media_unsupported:
			push("'type': 'unsupported'", NULL);
			break;
		case tgl_message_media_geo:
			push("'type': 'geo', 'longitude': %i, 'latitude': %i", M->geo.longitude, M->geo.latitude);
			break;
		case tgl_message_media_contact:
			push("'type': 'contact', 'phone': '%s', 'first_name': '%s', 'last_name': '%s', 'user_id': %i",  M->phone, M->first_name, M->last_name, M->user_id);
			break;
		default:
			push("'type': '???'", NULL);
			break;
	}
	push("}", NULL);
}
/*
void push_message (struct tgl_message *M) {
	assert (M);
	static char s[30];
	snprintf (s, 30, "%lld", M->id);
	lua_add_string_field ("id", s);
	if (!(M->flags & FLAG_CREATED)) { return; }
	lua_add_num_field ("flags", M->flags);

	if (tgl_get_peer_type (M->fwd_from_id)) {
		lua_pushstring (luaState, "fwd_from");
		push_peer (M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
		lua_settable (luaState, -3); // fwd_from

		lua_add_num_field ("fwd_date", M->fwd_date);
	}

	lua_pushstring (luaState, "from");
	push_peer (M->from_id, tgl_peer_get (TLS, M->from_id));
	lua_settable (luaState, -3);

	lua_pushstring (luaState, "to");
	push_peer (M->to_id, tgl_peer_get (TLS, M->to_id));
	lua_settable (luaState, -3);

	lua_pushstring (luaState, "out");
	lua_pushboolean (luaState, M->out);
	lua_settable (luaState, -3);

	lua_pushstring (luaState, "unread");
	lua_pushboolean (luaState, M->unread);
	lua_settable (luaState, -3);

	lua_pushstring (luaState, "date");
	lua_pushnumber (luaState, M->date);
	lua_settable (luaState, -3);

	lua_pushstring (luaState, "service");
	lua_pushboolean (luaState, M->service);
	lua_settable (luaState, -3);

	printf("{'id':%lld, 'flags': %i, 'forward':null, 'from':%s, 'to':%s, 'out':%s, 'unread':%s, 'date':%i, 'service':%s, 'text':'%s'}",
			M->id, M->flags, format_peer(M->from_id), format_peer(M->to_id),format_bool(M->out), format_bool(M->unread), M->date, format_bool(M->service), (M->message_len && M->message ?  M->message : ""));

	if (!M->service) {
		if (M->message_len && M->message) {
			lua_pushstring (luaState, "text");
			lua_pushlstring (luaState, M->message, M->message_len);
			lua_settable (luaState, -3);
		}
		if (M->media.type && M->media.type != tgl_message_media_none) {
			lua_pushstring (luaState, "media");
			push_media (&M->media);
			lua_settable (luaState, -3);
		}
	}
}

*/

/*
void print_message (struct in_ev *ev, struct tgl_message *M) {
	assert (M);
	if (M->flags & (FLAG_MESSAGE_EMPTY | FLAG_DELETED)) {
		return;
	}
	if (!(M->flags & FLAG_CREATED)) { return; }
	if (M->service) {
		print_service_message (ev, M);
		return;
	}
	if (!tgl_get_peer_type (M->to_id)) {
		logprintf ("Bad msg\n");
		return;
	}

	last_from_id = M->from_id;
	last_to_id = M->to_id;

	//print_start ();
	if (tgl_get_peer_type (M->to_id) == TGL_PEER_USER) {
		if (M->out) {
			mprintf (ev, "%lld ", M->id);
			print_date (ev, M->date);
			mpop_color (ev);
			mprintf (ev, " ");
			print_user_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
			mpush_color (ev, COLOR_GREEN);
			if (M->unread) {
				mprintf (ev, " <<< ");
			} else {
				mprintf (ev, " ««« ");
			}
		} else {
			mpush_color (ev, COLOR_BLUE);
			if (msg_num_mode) {
				mprintf (ev, "%lld ", M->id);
			}
			print_date (ev, M->date);
			mpop_color (ev);
			mprintf (ev, " ");
			print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
			mpush_color (ev, COLOR_BLUE);
			if (M->unread) {
				mprintf (ev, " >>> ");
			} else {
				mprintf (ev, " »»» ");
			}
		}
	} else if (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT) {
		tgl_peer_t *P = tgl_peer_get (TLS, M->to_id);
		assert (P);
		if (M->out) {
			mpush_color (ev, COLOR_GREEN);
			if (msg_num_mode) {
				mprintf (ev, "%lld ", M->id);
			}
			print_date (ev, M->date);
			mprintf (ev, " ");
			mpush_color (ev, COLOR_CYAN);
			mprintf (ev, " %s", P->print_name);
			mpop_color (ev);
			if (M->unread) {
				mprintf (ev, " <<< ");
			} else {
				mprintf (ev, " ««« ");
			}
		} else {
			mpush_color (ev, COLOR_BLUE);
			if (msg_num_mode) {
				mprintf (ev, "%lld ", M->id);
			}
			print_date (ev, M->date);
			mpush_color (ev, COLOR_CYAN);
			mprintf (ev, " %s", P->print_name);
			mpop_color (ev);
			if (M->unread) {
				mprintf (ev, " >>> ");
			} else {
				mprintf (ev, " »»» ");
			}
		}
	} else {
		assert (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT);
		mpush_color (ev, COLOR_MAGENTA);
		if (msg_num_mode) {
			mprintf (ev, "%lld ", M->id);
		}
		print_date (ev, M->date);
		mpop_color (ev);
		mprintf (ev, " ");
		print_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
		mprintf (ev, " ");
		print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
		if ((tgl_get_peer_type (M->from_id) == TGL_PEER_USER) && (tgl_get_peer_id (M->from_id) == TLS->our_id)) {
			mpush_color (ev, COLOR_GREEN);
		} else {
			mpush_color (ev, COLOR_BLUE);
		}
		if (M->unread) {
			mprintf (ev, " >>> ");
		} else {
			mprintf (ev, " »»» ");
		}
	}
	if (tgl_get_peer_type (M->fwd_from_id) == TGL_PEER_USER) {
		mprintf (ev, "[fwd from ");
		print_user_name (ev, M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
		mprintf (ev, "] ");
	}
	if (M->message && strlen (M->message)) {
		mprintf (ev, "%s", M->message);
	}
	if (M->media.type != tgl_message_media_none) {
		print_media (ev, &M->media);
	}
	mpop_color (ev);
	assert (!color_stack_pos);
	mprintf (ev, "\n");
	//print_end();
}


void lua_new_msg (struct tgl_message *M) {
	sprintf(socket_answer, "%i ", M->id,);
	355 [19 Jan] {print_message} chat#Ponies!!!#1145512 user#Danu Griese#31229512 »»» Wetter:{end_print_message}




}


struct tgl_message {
	struct tgl_message *next_use, *prev_use;
	struct tgl_message *next, *prev;
	long long id;
	int flags;
	tgl_peer_id_t fwd_from_id;
	int fwd_date;
	tgl_peer_id_t from_id;
	tgl_peer_id_t to_id;
	int out;
	int unread;
	int date;
	int service;
	union {
		struct tgl_message_action action;
		struct {
			char *message;
			int message_len;
			struct tgl_message_media media;
		};
	};
};



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
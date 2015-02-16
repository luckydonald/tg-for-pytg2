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
/*
Compile on my Mac system: (else just copy this code into lua-tg.c and use make as usual)
gcc -I. -I. -g -O2  -I/usr/local/include -I/usr/include -I/usr/include   -DHAVE_CONFIG_H -Wall -Wextra -Werror -Wno-deprecated-declarations -fno-strict-aliasing -fno-omit-frame-pointer -ggdb -Wno-unused-parameter -fPIC -c -MP -MD -MF dep/lua-tg.d -MQ objs/lua-tg.o -o objs/lua-tg.o msg-server-tg.c
make
bin/telegram-cli -s 127.0.0.1:4458




echo -e "\n\n\n" && gcc -I. -I. -g -O2  -I/usr/local/include -I/usr/include -I/usr/include   -DHAVE_CONFIG_H -Wall -Wextra -Werror -Wno-deprecated-declarations -fno-strict-aliasing -fno-omit-frame-pointer -ggdb -Wno-unused-parameter -fPIC -c -MP -MD -MF dep/lua-tg.d -MQ objs/lua-tg.o -o objs/lua-tg.o msg-server-tg.c && make && cp bin/telegram-cli /Users/tasso/Library/Caches/clion10/cmake/generated/3b825333/3b825333/Debug1/tg
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//colors
#include "interface.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

//only one socket at a time.
#include "sema.h"
//close():
#include <unistd.h>

//exit():
#include <stdlib.h>
#include "tgl/tgl-layout.h"
#include "tgl/tgl.h"

// while debug:
#include "tgl/tgl.h"
#include "tgl/generate.h"

// va_list, va_start, va_arg, va_end
#include <stdarg.h>
#include <arpa/inet.h>
#include <assert.h>

#include <sys/errno.h>
#include <Python/Python.h>



// A macro to shorten the error output to just the string used in perror.
#define DIE(error_string) \
 int err = errno;\
 perror(error_string);\
 exit(err);

#define SOCKET_ANSWER_MAX_SIZE (1 << 25)
#define BLOCK_SIZE 256
static char socket_answer[SOCKET_ANSWER_MAX_SIZE + 1];
static int answer_pos = -1;

int msg_freshness = -1; // -1: old (binlog), 0: startup (diff), 1: New
extern struct tgl_state *TLS;



//packet size
int socked_fd = -1;
struct sockaddr_in serv_addr;
int socked_in_use = 0;
struct rk_sema *edit_list;
struct rk_sema *edit_socket_status;

#define DEFAULT_PORT 4458

void socket_init (char *address_string);
int answer_started();
int answer_start();
void answer_end();
void socket_connect();
void socket_send();
void socket_close();

void lua_init(const char *address_string);
void lua_new_msg (struct tgl_message *M);
void lua_file_callback (struct tgl_state *TLSR, void *cb_extra, int success, char *file_name);

char* expand_escapes_alloc(const char* src);
char* malloc_formated(char const *format, ...);

void push_message (struct tgl_message *M);
static void push (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
//#define push(...) \
//  answer_add_printf (__VA_ARGS__)

void lua_init (const char *address_string) {
	char *address_string_copy = malloc(sizeof(char) * strnlen(address_string,23));
	strcpy(address_string_copy, address_string);
	socket_init(address_string_copy);
	edit_list = malloc(sizeof(struct rk_sema));
	edit_socket_status = malloc(sizeof(struct rk_sema));
	rk_sema_init(edit_list, 1); //can be lowered once.
	rk_sema_init(edit_socket_status, 1); //can be lowered once.
	//socket_connect();
	//socket_close();
}
union function_args {
	struct {
		int success; void *cb_extra; char *file_name;} file_download;
};
struct function {
	void (*callback)(union function_args *);
	union function_args *args;
	struct function *next;

};
struct function *get_last_function(struct function *func){
	assert(func);
	if (func->next == NULL) {
		return func;
	} else {
		return get_last_function(func->next);
	}
}
struct function *delayed_callbacks;

void append_function (struct function *func){
	rk_sema_wait(edit_list);
	if (delayed_callbacks == NULL) {
		delayed_callbacks = func;
	} else {
		get_last_function(delayed_callbacks)->next = func;
	}
	rk_sema_post(edit_list);
}
struct function *pop_function (){
	rk_sema_wait(edit_list);
	if (delayed_callbacks == NULL) {
		rk_sema_post(edit_list);
		return NULL;
	}
	struct function *tmp = delayed_callbacks;
	delayed_callbacks = delayed_callbacks->next;
	rk_sema_post(edit_list);
	return tmp;
}

void postpone(struct function *func) {
	append_function(func);
}
void postpone_execute_next() {
	struct function *func = pop_function();
	if(func != NULL)  // Queue is not empty.
	{
		void (*callback) (union function_args *) = func->callback;
		if (func->args)
		{
			union function_args *args = func->args;
			callback(args);
			free(func->args);
		}
		else
		{
			callback(NULL);
		}
	}
	free(func);
}

void lua_new_msg (struct tgl_message *M)
{
	assert(M);
	answer_start();
	printf("Sending Message...\n");
	push("{\"event\": \"message\", ");
	push_message (M);
	push("}");
	socket_connect();
	socket_send();
	socket_close();
	answer_end();
}


void lua_file_callbackback(union function_args *arg);

//actually is not external/lua call, but the defined callback.
void lua_file_callback (struct tgl_state *TLSR, void *cb_extra, int success, char *file_name) {
	union function_args *arg = malloc(sizeof(union function_args));
	arg->file_download.success = success;
	arg->file_download.cb_extra = cb_extra;
	if(file_name) {
		char *file_name_persistent = malloc_formated("%s",file_name); // + null-terminator
		arg->file_download.file_name = file_name_persistent;
	} else { //is NULL
		arg->file_download.file_name = NULL;
	}
	if (!answer_started()){
		lua_file_callbackback(arg);
		free(arg);
	}
	else {
		struct function *new = malloc(sizeof(struct function));
		new->callback = lua_file_callbackback;
		new->args = arg;
		new->next = NULL;
		postpone(new);
	}
}
void lua_file_callbackback(union function_args *arg) {
	if (answer_started()){
		struct function *new = malloc(sizeof(struct function));
		new->callback = lua_file_callbackback;
		union function_args *args = malloc(sizeof(union function_args));
		memcpy(args, arg, sizeof(union function_args));
		new->args = args;
		new->next = NULL;
		postpone(new);
		return;
	}
	answer_start();
	char *file_name = arg->file_download.file_name;
	long long int *msg_id = arg->file_download.cb_extra;
	int success = arg->file_download.success;
	if (success) {
		push("{\"event\":\"download\", \"id\":%lld, \"file\":\"%s\"}", *msg_id, file_name); //TODO: msg number.
	} else {
		push("{\"event\":\"download\", \"id\":%lld, \"file\":null}", *msg_id);
	}
	free(file_name);
	free(msg_id);
	socket_connect();
	socket_send();
	socket_close();
	answer_end();
}


/* parse the address and set the serv_addr */
void socket_init (char *address_string)
{
	if (address_string == NULL)
	{
		printf("No address and no port given.\n");
		exit(3);
	}
	char *port_pos = NULL;
	uint16_t port = DEFAULT_PORT;
	strtok(address_string, ":");
	port_pos = strtok(NULL, ":"); //why is it needed doubled?
	if (port_pos == NULL)
	{
		printf("Address: \"%s\", no port given, using port %i instead.\n", address_string, port);
	}
	else
	{
		port = atoi(port_pos);
		printf("Address: \"%s\", IP: %i.\n", address_string, port);
	}
	memset((void *) &serv_addr, 0, sizeof(struct sockaddr_in));
	serv_addr.sin_family = AF_INET; //still TCP
	inet_pton(AF_INET, address_string, &(serv_addr.sin_addr)); //copy the adress.
	serv_addr.sin_port = (in_port_t) htons((uint16_t) port);
}

void socket_connect() {
	while(socked_fd == -1)
	{
		socked_fd = socket(AF_INET, SOCK_STREAM, 0); //lets do UDP Socket and listener_d is the Descriptor
		if (socked_fd == -1)
		{
			perror("socket");
			sleep (1);
			continue; //new try.
		}
		else
		{
			printf("Socket opened.\n");
		}
		int connection = connect(socked_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
		if (connection == -1)
		{
			perror("connect");
			socket_close();
			sleep (1);
			continue;
		}
	}
}
int answer_started() {
	return socked_in_use;
}
int answer_start() {
	rk_sema_wait(edit_socket_status);
	if(socked_in_use) {
		rk_sema_post(edit_socket_status);
		return 0;
	}
	assert(socked_in_use == 0);
	answer_pos = 0;
	socked_in_use = 1;
	rk_sema_post(edit_socket_status);
	return 1;
}
void answer_end() {
	rk_sema_wait(edit_socket_status);
	assert(socked_in_use == 1);
	socked_in_use = 0;
	rk_sema_post(edit_socket_status);
}
void socket_send()
{
	if (answer_pos > 0) {
		printf("Message length: %i\n", answer_pos);
		printf("Sending response: " COLOR_GREY "%.*s" COLOR_NORMAL "\n", answer_pos, socket_answer);
		ssize_t sent = 0;
		int start = 0;
		size_t size = BLOCK_SIZE;
		if(answer_pos - start < (int)size) // less than a size block.
		{
			size = (size_t)(answer_pos - start); //what is left.
		}
		while( start < answer_pos)
		{
			//printf("send(%i, (void *)(%p  + %i)=%p, %li, 0)",socked_fd, &socket_answer, start, (void *)(socket_answer  + start),size);
			sent = send(socked_fd, (void *)(socket_answer  + start), size, 0);
			if(sent==-1){
				perror("send");
				break;
			}
			//printf("Send %li of %i, starting %i\n", sent, answer_pos, start);
			start += sent;
			if(answer_pos - start < (int)size) // less than a size block.
			{
				size = (size_t)(answer_pos - start); //what is left.
			}
			//printf("starting %i, going %li\n", start, size);
		}
		if (start == answer_pos)
		{
			puts("Did send.");
		} else {
			printf("Didn't send, start (%i) != answer_pos (%i)", start, answer_pos);
		}
		memset(socket_answer, 0, answer_pos); //reset da data.
		answer_pos = -1;
	}
}

void socket_close()
{
	if (socked_fd && socked_fd != -1)
	{
		close(socked_fd);
		socked_fd = -1;
		printf("Socket closed.\n");
	}else
	{
		printf("Socket was closed.\n");
	}
}

void push (const char *format, ...) {
	if (answer_pos < 0) { return; }
	va_list ap;
	va_start (ap, format);
	answer_pos += vsnprintf (socket_answer + answer_pos, SOCKET_ANSWER_MAX_SIZE - answer_pos, format, ap);
	va_end (ap);
	if (answer_pos >= SOCKET_ANSWER_MAX_SIZE) { answer_pos = -1; }
}




char *format_peer_type (int x) {
	/*
		one of this strings:
		"user", "group", "encr_chat"
		Please note, "group" is normaly called "chat"!
	*/
	switch (x) {
		case TGL_PEER_USER:
			return "user";
		case TGL_PEER_CHAT:
			return "group";
		case TGL_PEER_ENCR_CHAT:
			return "encr_chat";
		default:
			assert (0);
			return("Nope.avi");
	}
}
char *format_bool(int boolean) {
	return (boolean ? "true": "false");
}
char *format_string_or_null(char *str) {
	if (str == NULL) {
		return "";
	} else {
		return str;
	}
}

void push_peer (tgl_peer_id_t id);
void push_peer_cmd(tgl_peer_id_t id);

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
			push_peer (id);

		}
		push("]");
	} // end if
}

void push_encr_chat (tgl_peer_t *P) {
	push ("\"user\": ");
	push_peer (TGL_MK_USER (P->encr_chat.user_id));
}

void push_peer (tgl_peer_id_t id) {
	/*
	Will be { id: int, type: string, cmd: string }
	 */
	push("{");
	push("\"id\":%i, \"type\":\"%s\", \"cmd\": \"", tgl_get_peer_id (id), format_peer_type (tgl_get_peer_type (id)));
	push_peer_cmd(id);
	push("\", \"print_name\": ");
	//Note: opend quote for print_name's value!

	tgl_peer_t *P = tgl_peer_get(TLS, id);
	//P is defined -> did not return.
	if (P && (P->flags & FLAG_CREATED))
	{
		push("\"%s\", ", tgl_peer_get(TLS, id)->print_name);

		switch (tgl_get_peer_type(id))
		{
			case TGL_PEER_USER:
				push_user(tgl_peer_get(TLS, id));
				break;
			case TGL_PEER_CHAT:
				push_chat(tgl_peer_get(TLS, id));
				break;
			case TGL_PEER_ENCR_CHAT:
				push_encr_chat(tgl_peer_get(TLS, id));
				break;
			default:
				assert(0);
		}
	}else{
		push("null");
	}
	push(", \"flags\": %i}",  P->flags);
}

void push_peer_cmd(tgl_peer_id_t id) {
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


void push_media(struct tgl_message_media *M, long long int *msg_id)
{
	push("{");
	long long int *msg_id_copy;
	switch (M->type) {
		case tgl_message_media_photo:
			push("\"type\": \"photo\", \"encrypted\": false");
			if (M->photo.caption && strlen (M->photo.caption))
			{
				char *escaped_caption = expand_escapes_alloc(M->photo.caption);
				push (", \"caption\":\"%s\"", escaped_caption); //file name afterwards.
				free(escaped_caption);
			}
			msg_id_copy = malloc(sizeof(*msg_id));
			memcpy(msg_id_copy, msg_id, sizeof(*msg_id));
			tgl_do_load_photo (TLS, &M->photo, lua_file_callback, msg_id_copy);
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
			push("\"");
			msg_id_copy = malloc(sizeof(*msg_id));
			memcpy(msg_id_copy, msg_id, sizeof(*msg_id));
			tgl_do_load_document (TLS, &M->document, lua_file_callback, msg_id_copy); // will download & insert file name.
			if (M->document.caption && strlen (M->document.caption)) {
				char *escaped_caption = expand_escapes_alloc(M->document.caption);
				push(", \"caption\":\"%s\"", escaped_caption);
				free(escaped_caption);
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
			push("\""); //end of document's value, next is file name.
			msg_id_copy = malloc(sizeof(*msg_id));
			memcpy(msg_id_copy, msg_id, sizeof(*msg_id));
			tgl_do_load_encr_document (TLS, &M->encr_document, lua_file_callback, msg_id_copy); // will download & insert file name.
			//TODO: wait until the callback pushed the filename.
			if (M->encr_document.caption && strlen (M->document.caption)) {
				char *escaped_caption = expand_escapes_alloc(M->document.caption);
				push(", \"caption\":\"%s\"", escaped_caption);
				free(escaped_caption);
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
	if (!(M->flags & FLAG_CREATED)) {
		return;
	}
	push("\"id\":%lld, \"flags\": %i, \"forward\":", M->id, M->flags);
	if (tgl_get_peer_type (M->fwd_from_id)) {
		push("{\"sender\": ");
		push_peer (M->fwd_from_id);
		push(", \"date\": %i}", M->fwd_date);
	} else {
		push("null");
	}
	push(", \"sender\":");
	push_peer (M->from_id);
	push(", \"receiver\":");
	push_peer (M->to_id);
	if(!M->out && (tgl_get_peer_type(M->to_id) == TGL_PEER_CHAT || tgl_get_peer_type(M->to_id) == TGL_PEER_GEO_CHAT))
	{
		assert(tgl_get_peer_id(M->from_id) != TLS->our_id  && "Message should not be from ourself!");
		push(", \"peer\":");
		push_peer (M->to_id);
	} else if (!M->out && (tgl_get_peer_type(M->to_id) == TGL_PEER_USER || tgl_get_peer_type(M->to_id) == TGL_PEER_ENCR_CHAT)){
		assert(tgl_get_peer_id(M->to_id) == TLS->our_id && "Message should not be from ourself!");
		push(", \"peer\":");
		push_peer (M->from_id);
	} else {
		// own message (or something missed)
		push(", \"peer\": null");
	}
	push(", \"own\": %s, \"unread\":%s, \"date\":%i, \"service\":%s", format_bool(M->out), format_bool(M->unread), M->date, format_bool(M->service) );
	if (!M->service) {
		if (M->message_len > 0 && M->message) {
			char *escaped_message = expand_escapes_alloc(M->message);
			push(", \"text\": \"%s\"", escaped_message); // http://stackoverflow.com/a/3767300
			free(escaped_message);
		}
		if (M->media.type && M->media.type != tgl_message_media_none) {
			push(", \"media\":");
			push_media(&M->media, &M->id);
		}
	} else {
		push(", \"action\": %i",M->action.type);
	}
	// is no dict => no "}".
}
void push_client_id (struct tgl_message *M)
{
	push("client_id: %i",TLS->our_id);
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
// format (like printf) to a newly malloc'ed string.
char* malloc_formated(char const *format, ...) {
	va_list arglist, arglist_copy;
	va_start( arglist, format);
	size_t needed = vsnprintf(NULL, 0, format, arglist) + 1; //plus 1 for terminating NULL
	va_end(arglist);
	char  *buffer = malloc(needed);
	va_start( arglist_copy, format);
	vsnprintf(buffer, needed, format, arglist_copy);
	va_end( arglist );
	*(buffer + needed - 1) = '\0'; // ensure nul terminator
	return buffer;
}



// Empty stuff:
void lua_secret_chat_update (struct tgl_secret_chat *C, unsigned flags) {
	return;
}
void lua_user_update (struct tgl_user *U, unsigned flags) {
	printf("User Update (#%i \"%s\").\n", U->id.id, U->first_name);
	return;
}
void lua_diff_end (void) {
	/* Did all message since last session. */
	msg_freshness = 1;
	return;
}
void lua_do_all (void) {
	postpone_execute_next();
	return;
}
void lua_our_id (int id) {
	printf("lua_our_id: %i\n", id);
	return;
}
void lua_binlog_end (void) {
	/* Did all old messages.
	next comes new message (since last session -> lua_diff_end) */
	return;
}
void lua_chat_update (struct tgl_chat *C, unsigned flags) {
	return;
}

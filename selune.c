/* selune - selections manager
 * Copyright (C) 2022 ArcNyxx
 * see LICENCE file for licensing information */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

#define GP(var, atom, name) xcb_icccm_get_text_property_reply_t var;          \
	if (xcb_icccm_get_text_property_reply(con,                            \
			xcb_icccm_get_text_property(con, win, atom),          \
			&var, NULL) != 1)                                     \
		die("selune: unable to get property: %s\n", name);

typedef struct req {
	struct req *next;

	xcb_window_t win;
	xcb_atom_t atom;
	size_t pos;
} req_t;

typedef enum atoms {
	ASL, ATR, AAT, AIN, ATS, ATM, ANT, APL, ALS
} atoms_t;

static void die(const char *fmt, ...);
static void *srealloc(void *ptr, size_t len);
static char *getsel(xcb_window_t win, size_t *len, xcb_timestamp_t *time);
static bool send(xcb_generic_event_t *evt,
		char *buf, size_t len, size_t maxlen, xcb_timestamp_t time);

static xcb_connection_t *con;
static xcb_atom_t at[ALS];
static req_t *reqs = NULL;

static void
die(const char *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);

	if (fmt[strlen(fmt) - 1] != '\n')
		perror(NULL);
	if (con != NULL)
		xcb_disconnect(con);
	exit(1);
}

static void *
srealloc(void *ptr, size_t len)
{
	if ((ptr = realloc(ptr, len)) == NULL)
		die("selune: unable to allocate memory: ");
	return ptr;
}

static char *
getsel(xcb_window_t win, size_t *len, xcb_timestamp_t *time)
{
	xcb_generic_event_t *evt;
	xcb_convert_selection(con, win, at[ASL], at[ATR], at[APL],
			XCB_CURRENT_TIME);
	xcb_flush(con);
	while ((evt = xcb_wait_for_event(con)) != NULL &&
			(evt->response_type & ~0x80) != XCB_SELECTION_NOTIFY)
		free(evt);
	if (((xcb_selection_notify_event_t *)evt)->property == XCB_NONE)
		die("selune: unable to convert selection\n");

	xcb_get_property_reply_t *proc;
	if ((proc = xcb_get_property_reply(con, xcb_get_property(con,
			false, win, at[APL], XCB_GET_PROPERTY_TYPE_ANY,
			0, 0), NULL)) == NULL)
		die("selune: unable to get property type\n");
	bool incr = proc->type == at[AIN];
	free(proc);

	char *buf = NULL;
	*time = ((xcb_selection_notify_event_t *)evt)->time;
	if (!incr) {
		GP(prop, at[APL], "PLAC")
		if (prop.name_len == 0)
			die("selune: unable to read empty input\n");
		buf = srealloc(buf, *len += prop.name_len);
		memcpy(buf, prop.name, prop.name_len);
		xcb_icccm_get_text_property_reply_wipe(&prop);
		xcb_delete_property(con, win, at[APL]);
		return buf;
	}

	for (;;) {
		xcb_delete_property(con, win, at[APL]);
		while ((evt = xcb_wait_for_event(con)) != NULL && (evt->
				response_type & ~0x80) != XCB_PROPERTY_NOTIFY
				&& ((xcb_property_notify_event_t *)evt)->state
				!= XCB_PROPERTY_NEW_VALUE)
			free(evt);

		GP(prop, at[APL], "PLAC")
		if (prop.name_len == 0) {
			xcb_icccm_get_text_property_reply_wipe(&prop);
			xcb_delete_property(con, win, at[APL]);
			return buf;
		}
		buf = srealloc(buf, *len += prop.name_len);
		memcpy(buf, prop.name, prop.name_len);
		xcb_icccm_get_text_property_reply_wipe(&prop);
	}
}

static bool
send(xcb_generic_event_t *evt, char *buf, size_t len, size_t maxlen,
		xcb_timestamp_t time)
{
	if ((evt->response_type & ~0x80) == XCB_SELECTION_CLEAR)
		return false;
	if ((evt->response_type & ~0x80) == XCB_PROPERTY_NOTIFY) {
		xcb_property_notify_event_t *ev =
				(xcb_property_notify_event_t *)evt;
		if (ev->state != XCB_PROPERTY_DELETE)
			return true;

		req_t *req;
		for (req = reqs; req != NULL && (req->win != ev->window ||
				req->atom != ev->atom); req = req->next);
		if (req == NULL) return true;

		if (xcb_request_check(con, xcb_change_property_checked(
				con, XCB_PROP_MODE_REPLACE, req->win, ev->atom,
				at[ATR], 8, maxlen > len - req->pos ? maxlen :
				req->pos, &buf[req->pos])) != NULL)
			die("selune: unable to change property\n");
		if (req->pos == len) {
			req_t *nr;
			for (nr = reqs; nr->next != req; nr = nr->next);
			nr->next = req->next;
			free(req);
		}

		req->pos = maxlen > len - req->pos ? len - req->pos : maxlen;
		return true;
	}
	if ((evt->response_type & ~0x80) != XCB_SELECTION_REQUEST)
		return true;

	xcb_selection_request_event_t *ev =
			(xcb_selection_request_event_t *)evt;
	xcb_selection_notify_event_t sev = { XCB_SELECTION_NOTIFY, 0, 0,
			ev->time, ev->requestor, ev->selection,
			ev->target, ev->property };

	void *ptr = buf;
	size_t size = 8;
	xcb_atom_t type = at[ATR];
	xcb_atom_t trgarr[] = { at[ATR], at[ATS], at[ATM] };
	if (/* (ev->time < time && ev->time != XCB_CURRENT_TIME) || */
			ev->property == XCB_NONE) {
		sev.property = XCB_NONE;
	} else if (ev->target == at[ATS]) {
		size = 32; len = 3; ptr = trgarr; type = at[AAT];
	} else if (ev->target == at[ATM]) {
		size = 32; len = 1; ptr = &time; type = at[ANT];
	} else if (len > maxlen) {
		req_t *req = srealloc(NULL, sizeof(req_t));
		req->win = ev->requestor, req->atom = ev->property;
		req->pos = 0, req->next = reqs;
		reqs = req;

		size = 32; len = 0; ptr = NULL; type = at[AIN];
	}

	xcb_generic_error_t *err;
	if (sev.property != XCB_NONE && (err = xcb_request_check(con,
			xcb_change_property_checked(con,
			XCB_PROP_MODE_REPLACE, ev->requestor, ev->property,
			type, size, len, ptr))) != NULL) {
		sev.property = XCB_NONE;
		free(err);
	}
	xcb_send_event(con, false, ev->requestor, 0, (char *)&sev);
	xcb_flush(con);
	return true;
}

int
main(int argc, char **argv)
{
	const char *sel = "CLIPBOARD", *trg = "UTF8_STRING";
	if (*(argv = &argv[1]) != NULL && (*argv)[0] == '-') {
		bool end = false; char *str = argv[0];
		for (int i = 1; str[i] != '\0'; ++i)
			switch (str[i]) {
			case 'c': sel = "CLIPBOARD"; break;
			case 'p': sel = "PRIMARY";   break;
			case 's': sel = "SECONDARY"; break;
			case 't':
				if (!end && (trg = *(argv = &argv[1])) != NULL)
					end = true;
				break;
			case 'x':
				if (!end && (sel = *(argv = &argv[1])) != NULL)
					end = true;
				break;
			default:
				die("selune: invalid option: -%c\n", str[i]);
			}
	}

	int scrnum;
	con = xcb_connect(NULL, &scrnum);
	xcb_screen_iterator_t iter =
			xcb_setup_roots_iterator(xcb_get_setup(con));
	for (int i = 0; i < scrnum; ++i)
		xcb_screen_next(&iter);
	xcb_screen_t *scr = iter.data;

	xcb_window_t win = xcb_generate_id(con);
	if (xcb_request_check(con, xcb_create_window_checked(con,
			scr->root_depth, win, scr->root, 0, 0, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
			XCB_CW_EVENT_MASK, &(uint32_t){
			XCB_EVENT_MASK_PROPERTY_CHANGE })) != NULL)
		die("selune: unable to create window\n");

#define GA(call, name)                                                        \
	xcb_intern_atom_cookie_t call ## _cookie =                            \
			xcb_intern_atom(con, false, strlen(name), name);
#define PA(call, name)                                                        \
	xcb_intern_atom_reply_t *call ## _reply;                              \
	if ((call ## _reply = xcb_intern_atom_reply(con, call ## _cookie,     \
			NULL)) == NULL)                                       \
		die("selune: unable to create atom: %s\n", name);             \
	at[loc] = call ## _reply->atom; ++loc;                                 \
	free(call ## _reply);

	int loc = 0;
	GA(a, sel) GA(b, trg) GA(c, "ATOM") GA(d, "INCR") GA(e, "TARGETS")
	GA(f, "TIMESTAMP") GA(g, "INTEGER") GA(h, "PLAC")
	PA(a, sel) PA(b, trg) PA(c, "ATOM") PA(d, "INCR") PA(e, "TARGETS")
	PA(f, "TIMESTAMP") PA(g, "INTEGER") PA(h, "PLAC")

	size_t len = 0;
	char *buf = NULL;
	xcb_timestamp_t time;
	if (isatty(STDIN_FILENO)) {
		buf = getsel(win, &len, &time);
	} else {
		long ret;
		size_t tot = 256;
		buf = srealloc(buf, tot);
		while ((ret = read(STDIN_FILENO, &buf[len], tot - len)) != 0)
			if (ret == -1)
				die("selune: unable to read from pipe: ");
			else if (tot == (len += ret))
				buf = srealloc(buf, tot *= 2);
		if (len == 0)
			die("selune: unable to read empty input\n");
		buf = srealloc(buf, len);

		xcb_generic_event_t *evt;
		xcb_change_property(con, XCB_PROP_MODE_APPEND, win, at[APL],
				at[APL], 8, 0, NULL);
		xcb_flush(con);
		while ((evt = xcb_wait_for_event(con)) != NULL && (evt->
				response_type & ~0x80) != XCB_PROPERTY_NOTIFY)
			free(evt);
		time = ((xcb_property_notify_event_t *)evt)->time;
		free(evt);
	}
	write(STDOUT_FILENO, buf, len);

	if (fork() != 0)      return 0;
	if (chdir("/") == -1) die("selune: unable to chdir: /: ");

	xcb_get_selection_owner_reply_t *gow;
	if (xcb_request_check(con, xcb_set_selection_owner(con,
			win, at[ASL], time)) != NULL)
		die("selune: unable to change selection ownership\n");
	if ((gow = xcb_get_selection_owner_reply(con, xcb_get_selection_owner(
			con, at[ASL]), NULL)) == NULL || gow->owner != win)
		die("selune: unable to confirm selection ownership\n");
	free(gow);

	bool rn = true;
	xcb_generic_event_t *evt = NULL;
	size_t maxlen = xcb_get_maximum_request_length(con) / 8 * 7;
	for (; (rn || reqs != NULL) &&
			(evt = xcb_wait_for_event(con)) != NULL; free(evt))
		rn = send(evt, buf, len, maxlen, time) && rn;
	xcb_destroy_window(con, win);
	xcb_disconnect(con);
}

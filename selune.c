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

#define GA(call, name)                                                        \
	xcb_intern_atom_cookie_t call ## _cookie =                            \
			xcb_intern_atom(conn, false, strlen(name), name);
#define PA(call, name)                                                        \
	xcb_intern_atom_reply_t *call ## _reply;                              \
	if ((call ## _reply = xcb_intern_atom_reply(conn, call ## _cookie,    \
			NULL)) == NULL)                                       \
		die("selune: unable to create atom: %s\n", name);             \
	a[loc] = call ## _reply->atom; ++loc;                                 \
	free(call ## _reply);
#define GP(var, atom, name) xcb_icccm_get_text_property_reply_t var;          \
	if (xcb_icccm_get_text_property_reply(conn,                           \
			xcb_icccm_get_text_property(conn, win, atom),         \
			&var, NULL) != 1)                                     \
		die("selune: unable to get property: %s\n", name);

typedef struct req {
	xcb_window_t win;
	xcb_atom_t atom;
	size_t pos;
} req_t;

typedef enum atom {
	A_SEL, A_TRG, A_ATOM, A_INCR, A_TARGS,
	A_MULT, A_TIMES, A_INT, A_PLAC, A_LAST
} atom_t;

static void die(const char *fmt, ...);
static void *srealloc(void *ptr, size_t len);
static char *getsel(xcb_window_t win, size_t *len, xcb_timestamp_t *time);
static bool send(xcb_generic_event_t *evt, xcb_timestamp_t time, char *buf,
		size_t len, size_t maxlen);

static xcb_connection_t *conn;
static xcb_atom_t a[A_LAST];

static void
die(const char *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);

	if (fmt[strlen(fmt) - 1] != '\n')
		perror(NULL);
	if (conn != NULL)
		xcb_disconnect(conn);
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
	xcb_generic_event_t *ev;
	xcb_convert_selection(conn, win,
			a[A_SEL], a[A_TRG], a[A_PLAC], XCB_CURRENT_TIME);
	xcb_flush(conn);
	while ((ev = xcb_wait_for_event(conn)) != NULL &&
			(ev->response_type & ~0x80) != XCB_SELECTION_NOTIFY)
		free(ev);
	if (((xcb_selection_notify_event_t *)ev)->property == XCB_NONE)
		die("selune: unable to convert selection\n");

	char *buf = NULL;
	*time = ((xcb_selection_notify_event_t *)ev)->time;
	if (((xcb_selection_notify_event_t *)ev)->target != a[A_INCR]) {
		GP(prop, a[A_PLAC], "PLAC")
		if (prop.name_len == 0)
			die("selune: unable to read empty input\n");
		buf = srealloc(buf, *len += prop.name_len);
		memcpy(buf, prop.name, prop.name_len);
		xcb_icccm_get_text_property_reply_wipe(&prop);
		xcb_delete_property(conn, win, a[A_PLAC]);
		return buf;
	}

	for (;;) {
		xcb_delete_property(conn, win, a[A_PLAC]);
		while ((ev = xcb_wait_for_event(conn)) != NULL && (ev->
				response_type & ~0x80) != XCB_PROPERTY_NOTIFY
				&& ((xcb_property_notify_event_t *)ev)->state
				!= XCB_PROPERTY_NEW_VALUE)
			free(ev);

		GP(prop, a[A_PLAC], "PLAC")
		if (prop.name_len == 0) {
			xcb_icccm_get_text_property_reply_wipe(&prop);
			xcb_delete_property(conn, win, a[A_PLAC]);
			return buf;
		}
		buf = srealloc(buf, *len += prop.name_len);
		memcpy(buf, prop.name, prop.name_len);
		xcb_icccm_get_text_property_reply_wipe(&prop);
	}
}

static bool
send(xcb_generic_event_t *evt, xcb_timestamp_t time, char *buf,
		size_t len, size_t maxlen)
{
	switch (evt->response_type & ~0x80) {
	case XCB_SELECTION_REQUEST: {
		xcb_selection_request_event_t *ev =
				(xcb_selection_request_event_t *)evt;
		xcb_selection_notify_event_t sev = { XCB_SELECTION_NOTIFY,
				0, 0, ev->time, ev->requestor, ev->selection,
				ev->target, ev->property };

		if (/* (ev->time < time && ev->time != XCB_CURRENT_TIME) || */
				ev->property == XCB_NONE) {
			sev.property = XCB_NONE;
			goto send;
		}

		void *ptr;
		size_t size = 8;
		xcb_atom_t type = a[A_ATOM];
		xcb_atom_t trgarr[] = {
				a[A_TRG], a[A_TARGS], a[A_MULT], a[A_TIMES] };

		if (ev->target == a[A_TARGS]) {
			size = 32; len = 4; ptr = trgarr;
		} else if (ev->target == a[A_MULT]) {
			die("UNIMPLEMENTED\n");
		} else if (ev->target == a[A_TIMES]) {
			size = 32; len = 1; ptr = &time; type = a[A_INT];
		} else if (len > maxlen) {
			die("UNIMPLEMENTED\n");
		} else {
			ptr = buf; type = a[A_TRG];
		}

		xcb_generic_error_t *err;
		if ((err = xcb_request_check(conn, xcb_change_property_checked(
				conn, XCB_PROP_MODE_REPLACE, ev->requestor, ev
				->property, type, size, len, ptr))) != NULL) {
			sev.property = XCB_NONE; free(err);
		}
send:
		xcb_send_event(conn, false, ev->requestor, 0, (char *)&sev);
		xcb_flush(conn);
		return false;
	}
	case XCB_PROPERTY_NOTIFY: {
		break;
	}
	case XCB_SELECTION_CLEAR: {
		return true;
	}
	default: ;
	}
	return false;
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
	conn = xcb_connect(NULL, &scrnum);
	xcb_screen_iterator_t iter =
			xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (int i = 0; i < scrnum; ++i)
		xcb_screen_next(&iter);
	xcb_screen_t *scr = iter.data;

	xcb_window_t win = xcb_generate_id(conn);
	if (xcb_request_check(conn, xcb_create_window_checked(conn,
			scr->root_depth, win, scr->root, 0, 0, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
			XCB_CW_EVENT_MASK, &(uint32_t){ 
			XCB_EVENT_MASK_PROPERTY_CHANGE })) != NULL)
		die("selune: unable to create window\n");

	int loc = 0;
	GA(j, sel) GA(b, trg) GA(c, "ATOM") GA(d, "INCR") GA(e, "TARGETS")
	GA(f, "MULTIPLE") GA(g, "TIMESTAMP") GA(h, "INTEGER") GA(i, "PLAC")
	PA(j, sel) PA(b, trg) PA(c, "ATOM") PA(d, "INCR") PA(e, "TARGETS")
	PA(f, "MULTIPLE") PA(g, "TIMESTAMP") PA(h, "INTEGER") PA(i, "PLAC")

	size_t len = 0;
	char *buf = NULL;
	xcb_timestamp_t time;
	if (isatty(STDIN_FILENO)) {
		buf = getsel(win, &len, &time);
	} else {
		long ret; size_t tot;
		buf = srealloc(buf, tot = 256);
		while ((ret = read(STDIN_FILENO, &buf[len], tot - len)) != 0) {
			if (ret == -1)
				die("selune: unable to read from pipe: ");
			if (tot == (len += ret))
				buf = srealloc(buf, tot *= 2);
		}
		if (len == 0)
			die("selune: unable to read empty input\n");
		buf = srealloc(buf, len);

		xcb_generic_event_t *ev;
		xcb_change_property(conn, XCB_PROP_MODE_APPEND, win,
				a[A_TIMES], a[A_TIMES], 8, 0, NULL);
		xcb_flush(conn);
		while ((ev = xcb_wait_for_event(conn)) != NULL && (ev->
				response_type & ~0x80) != XCB_PROPERTY_NOTIFY)
			free(ev);
		time = ((xcb_property_notify_event_t *)ev)->time;
		free(ev);
	}
	write(STDOUT_FILENO, buf, len);

	if (fork() != 0)      return 0;
	if (chdir("/") == -1) die("selune: unable to chdir: /: ");

	xcb_get_selection_owner_reply_t *gep;
	if (xcb_request_check(conn, xcb_set_selection_owner(conn,
			win, a[A_SEL], time)) != NULL)
		die("selune: unable to change selection ownership\n");
	if ((gep = xcb_get_selection_owner_reply(conn, xcb_get_selection_owner(
			conn, a[A_SEL]), NULL)) == NULL || gep->owner != win)
		die("selune: unable to confirm selection ownership\n");
	free(gep);

	bool done = false;
	xcb_generic_event_t *ev;
	size_t maxlen = xcb_get_maximum_request_length(conn) / 8 * 7;
	while (!done && (ev = xcb_wait_for_event(conn)) != NULL) {
		done = send(ev, time, buf, len, maxlen);
		free(ev);
	}

	xcb_destroy_window(conn, win);
	xcb_disconnect(conn);
}

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

#define GA(var, name, len) xcb_intern_atom_cookie_t var ## _cookie =          \
	xcb_intern_atom(conn, false, len, name);
#define CA(var, name) xcb_intern_atom_reply_t *var ## _reply;                 \
	if ((var ## _reply = xcb_intern_atom_reply(conn, var ## _cookie,      \
			NULL)) == NULL)                                       \
		die("selune: unable to create atom: %s\n", name);             \
	xcb_atom_t var = var ## _reply->atom;                                 \
	free(var ## _reply);
#define GP(var, atom, name) xcb_icccm_get_text_property_reply_t var;          \
	if (xcb_icccm_get_text_property_reply(conn,                           \
			xcb_icccm_get_text_property(conn, win, atom),         \
			&var, NULL) != 1)                                     \
		die("selune: unable to get property: %s\n", name);

typedef struct req {
	xcb_window_t win;
	xcb_atom_t atom;
	size_t pos;
	bool incr;
} req_t;

static void die(const char *fmt, ...);
static void *srealloc(void *ptr, size_t len);
static char *getsel(xcb_window_t win, xcb_atom_t sel, xcb_atom_t trg,
		xcb_atom_t incr, size_t *len, xcb_timestamp_t *time);
static bool send(xcb_generic_event_t *evt, xcb_atom_t trg,
		xcb_timestamp_t time, char *buf, size_t len, size_t maxlen)

static xcb_connection_t *conn;

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
getsel(xcb_window_t win, xcb_atom_t sel, xcb_atom_t trg,
		xcb_atom_t incr, size_t *len, xcb_timestamp_t *time)
{
	xcb_generic_event_t *evt;
	GA(plac, "PLAC", 4) CA(plac, "PLAC")
	xcb_convert_selection(conn, win, sel, trg, plac, XCB_CURRENT_TIME);
	xcb_flush(conn);
	while ((evt = xcb_wait_for_event(conn)) != NULL &&
			(evt->response_type & ~0x80) != XCB_SELECTION_NOTIFY)
		free(evt);
	if (((xcb_selection_notify_event_t *)evt)->property == XCB_NONE)
		die("selune: unable to convert selection\n");

	char *buf = NULL; *time = ((xcb_selection_notify_event_t *)evt)->time;
	if (((xcb_selection_notify_event_t *)evt)->target != incr) {
		GP(prop, plac, "PLAC")
		buf = srealloc(buf, *len += prop.name_len);
		memcpy(buf, prop.name, prop.name_len);
		xcb_icccm_get_text_property_reply_wipe(&prop);
		xcb_delete_property(conn, win, plac);
		return buf;
	}

run:
	xcb_delete_property(conn, win, plac);
	while ((evt = xcb_wait_for_event(conn)) != NULL &&
			(evt->response_type & ~0x80) != XCB_PROPERTY_NOTIFY &&
			((xcb_property_notify_event_t *)evt)->state != 
			XCB_PROPERTY_NEW_VALUE)
		free(evt);

	GP(prop, plac, "PLAC")
	if (prop.name_len != 0) {
		buf = srealloc(buf, *len += prop.name_len);
		memcpy(buf, prop.name, prop.name_len);
		xcb_icccm_get_text_property_reply_wipe(&prop);
		goto run;
	}
	xcb_icccm_get_text_property_reply_wipe(&prop);
	xcb_delete_property(conn, win, plac);
	return buf;
}

static bool
send(xcb_generic_event_t *evt, xcb_atom_t trg, xcb_timestamp_t time,
		char *buf, size_t len, size_t maxlen)
{
	switch (evt->response_type & ~0x80) {
	case XCB_SELECTION_REQUEST: {
		xcb_generic_error_t *err;
		xcb_selection_request_event_t *ev =
				(xcb_selection_request_event_t *)evt;
		xcb_selection_notify_event_t sev = { XCB_SELECTION_NOTIFY,
				0, 0, ev->time, ev->requestor,
				ev->selection, ev->target, ev->property };

		if (ev->time < time || ev->property == XCB_NONE) {
			sev.property = XCB_NONE;
		} else if (len > maxlen) {
			die("UNIMPLEMENTED\n");
		} else if ((err = xcb_request_check(conn,
				xcb_change_property_checked(conn,
				XCB_PROP_MODE_REPLACE, ev->requestor,
				ev->property, trg, 8, len, buf))) != NULL) {
			sev.property = XCB_NONE; free(err);
		}
		xcb_send_event(conn, false, ev->requestor, 0, (char *)&sev);
		xcb_flush(conn);
		return false;
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

	GA(asel, sel, strlen(sel)) GA(atrg, trg, strlen(trg))
	GA(incr, "INCR", 4) GA(targ, "TARGETS", 7) GA(mult, "MULTIPLE", 8)
	GA(tims, "TIMESTAMP", 9) CA(asel, sel) CA(atrg, trg) CA(incr, "INCR")
	CA(targ, "TARGETS") CA(mult, "MULTIPLE")  CA(tims, "TIMESTAMP")

	size_t len = 0; char *buf = NULL; xcb_timestamp_t time;
	if (isatty(STDIN_FILENO)) {
		buf = getsel(win, asel, atrg, incr, &len, &time);
	} else {
		long ret, tot;
		buf = srealloc(buf, tot = 256);
		while ((ret = read(STDIN_FILENO, &buf[len], tot - len)) ==
				(long)(tot - len))
			buf = srealloc(buf, tot *= 2), len += ret;
		if (ret == -1)
			die("selune: unable to read from stdin: ");
		buf = srealloc(buf, len += ret);

		xcb_generic_event_t *evt;
		xcb_change_property(conn, XCB_PROP_MODE_APPEND, win,
				atrg, atrg, 8, 0, NULL);
		while ((evt = xcb_wait_for_event(conn)) != NULL &&
				(evt->response_type & ~0x80) !=
				XCB_PROPERTY_NOTIFY)
			free(evt);
		time = ((xcb_property_notify_event_t *)evt)->time; free(evt);
	}

	if (len != 0)         write(STDOUT_FILENO, buf, len);
	// if (fork() != 0)      return 0;
	// if (chdir("/") == -1) die("selune: unable to chdir: /: ");

	xcb_get_selection_owner_reply_t *gep;
	xcb_set_selection_owner(conn, win, asel, time);
	if ((gep = xcb_get_selection_owner_reply(conn, xcb_get_selection_owner(
			conn, asel), NULL)) == NULL || gep->owner != win)
		die("selune: unable to confirm selection ownership\n");
	free(gep);

	xcb_generic_event_t *evt;
	size_t maxlen = xcb_get_maximum_request_length(conn) * 3 / 4;
	while ((evt = xcb_wait_for_event(conn)) != NULL) {
		if (send(evt, atrg, time, buf, len, maxlen))
			break;
		free(evt);
	}
	free(evt);

	xcb_destroy_window(conn, win);
	xcb_disconnect(conn);
}

/* SPDX-License-Identifier: BSD-2-Clause */
/* Included by kern_exec.c after EXECUTABLE_KEY/exec_add_user_string.
 * Diagnostic environment only: no loader, trust or entitlement changes. */
#ifndef B2L_EXEC_GRAPHICS_DIAG_H
#define B2L_EXEC_GRAPHICS_DIAG_H
static int
b2l_exec_graphics_diag(struct image_params *imgp, int pointer_size)
{
	static const char setting[] = "CG_CONTEXT_SHOW_BACKTRACE=1";
	static const char key[] = "CG_CONTEXT_SHOW_BACKTRACE=";
	int enabled = 0;
	if (!PE_parse_boot_argn("b2l_diag", &enabled, sizeof(enabled)) || enabled != 1 ||
	    (pointer_size != 4 && pointer_size != 8)) return 0;
	const char *path = imgp->ip_strings + strlen(EXECUTABLE_KEY);
	if (strcmp(path, "/System/Library/CoreServices/SpringBoard.app/SpringBoard") &&
	    strcmp(path, "/usr/libexec/backboardd") &&
	    strcmp(path, "/Applications/Preferences.app/Preferences")) return 0;
	/* Preserve a caller-provided value and inspect only already copied,
	 * NUL-terminated env strings, never original user pointers. */
	const char *cursor = imgp->ip_endargv;
	while (cursor < imgp->ip_strendp) {
		size_t available = (size_t)(imgp->ip_strendp - cursor);
		size_t length = strnlen(cursor, available);
		if (length == available) return 0;
		if (length >= sizeof(key) - 1 &&
		    memcmp(cursor, key, sizeof(key) - 1) == 0) return 0;
		cursor += length + 1;
	}
	/* Leave the normal env terminator and tail alignment ample space. A
	 * diagnostics-only addition must not push an otherwise valid exec over
	 * NCARGS. The regular checked string copier accounts for string bytes. */
	if (imgp->ip_argspace < (int)(sizeof(setting) + 3 * pointer_size) ||
	    imgp->ip_strspace < (int)sizeof(setting)) return 0;
	int error = exec_add_user_string(imgp, CAST_USER_ADDR_T(setting), UIO_SYSSPACE, TRUE);
	if (error) return error;
	imgp->ip_argspace -= pointer_size;
	imgp->ip_envc++;
	b2l_early_trace("B2L CG DIAG: backtrace environment enabled for %s\n", path);
	return 0;
}
#endif

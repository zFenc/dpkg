/*
 * dpkg - main program for package management
 * statdb.c - management of database of ownership and mode of files
 *
 * Copyright © 1995 Ian Jackson <ian@chiark.greenend.org.uk>
 * Copyright © 2000, 2001 Wichert Akkerman <wakkerma@debian.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with dpkg; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <compat.h>

#include <dpkg/i18n.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include <dpkg.h>
#include <dpkg-db.h>

#include "filesdb.h"
#include "main.h"

static FILE *statoverridefile = NULL;

uid_t
statdb_parse_uid(const char *str)
{
	char* endptr;
	uid_t uid;

	if (str[0] == '#') {
		uid = strtol(str + 1, &endptr, 10);
		if (str + 1 == endptr || *endptr)
			ohshit(_("syntax error: invalid uid in statoverride file"));
	} else {
		struct passwd* pw = getpwnam(str);
		if (pw == NULL)
			ohshit(_("syntax error: unknown user '%s' in statoverride file"),
			       str);
		uid = pw->pw_uid;
	}

	return uid;
}

gid_t
statdb_parse_gid(const char *str)
{
	char* endptr;
	gid_t gid;

	if (str[0] == '#') {
		gid = strtol(str + 1, &endptr, 10);
		if (str + 1 == endptr || *endptr)
			ohshit(_("syntax error: invalid gid in statoverride file"));
	} else {
		struct group* gr = getgrnam(str);
		if (gr == NULL)
			ohshit(_("syntax error: unknown group '%s' in statoverride file"),
			       str);
		gid = gr->gr_gid;
	}

	return gid;
}

mode_t
statdb_parse_mode(const char *str)
{
	char* endptr;
	mode_t mode;

	mode = strtol(str, &endptr, 8);
	if (str == endptr || *endptr)
		ohshit(_("syntax error: invalid mode in statoverride file"));

	return mode;
}

void
ensure_statoverrides(void)
{
	static struct varbuf vb;

	struct stat stab1, stab2;
	FILE *file;
	char *loaded_list, *loaded_list_end, *thisline, *nextline, *ptr;
	struct filestatoverride *fso;
	struct filenamenode *fnn;

	varbufreset(&vb);
	varbufaddstr(&vb, admindir);
	varbufaddstr(&vb, "/" STATOVERRIDEFILE);
	varbufaddc(&vb, 0);

	onerr_abort++;

	file = fopen(vb.buf,"r");
	if (!file) {
		if (errno != ENOENT)
			ohshite(_("failed to open statoverride file"));
		if (!statoverridefile) {
			onerr_abort--;
			return;
		}
	} else {
		if (fstat(fileno(file), &stab2))
			ohshite(_("failed to fstat statoverride file"));
		if (statoverridefile) {
			if (fstat(fileno(statoverridefile), &stab1))
				ohshite(_("failed to fstat previous statoverride file"));
			if (stab1.st_dev == stab2.st_dev &&
			    stab1.st_ino == stab2.st_ino) {
				fclose(file);
				onerr_abort--;
				return;
			}
		}
	}
	if (statoverridefile)
		fclose(statoverridefile);
	statoverridefile = file;
	setcloexec(fileno(statoverridefile), vb.buf);

	/* If the statoverride list is empty we don't need to bother
	 * reading it. */
	if (!stab2.st_size) {
		onerr_abort--;
		return;
	}

	loaded_list = nfmalloc(stab2.st_size);
	loaded_list_end = loaded_list + stab2.st_size;

	fd_buf_copy(fileno(file), loaded_list, stab2.st_size,
	            _("statoverride file `%.250s'"), vb.buf);

	thisline = loaded_list;
	while (thisline < loaded_list_end) {
		fso = nfmalloc(sizeof(struct filestatoverride));

		if (!(ptr = memchr(thisline, '\n', loaded_list_end - thisline)))
			ohshit(_("statoverride file is missing final newline"));
		/* Where to start next time around. */
		nextline = ptr + 1;
		if (ptr == thisline)
			ohshit(_("statoverride file contains empty line"));
		*ptr = '\0';

		/* Extract the uid. */
		if (!(ptr = memchr(thisline, ' ', nextline - thisline)))
			ohshit(_("syntax error in statoverride file"));
		*ptr = '\0';

		fso->uid = statdb_parse_uid(thisline);

		/* Move to the next bit */
		thisline = ptr + 1;
		if (thisline >= loaded_list_end)
			ohshit(_("unexpected end of line in statoverride file"));

		/* Extract the gid */
		if (!(ptr = memchr(thisline, ' ', nextline - thisline)))
			ohshit(_("syntax error in statoverride file"));
		*ptr = '\0';

		fso->gid = statdb_parse_gid(thisline);

		/* Move to the next bit */
		thisline = ptr + 1;
		if (thisline >= loaded_list_end)
			ohshit(_("unexpected end of line in statoverride file"));

		/* Extract the mode */
		if (!(ptr = memchr(thisline, ' ', nextline - thisline)))
			ohshit(_("syntax error in statoverride file"));
		*ptr = '\0';

		fso->mode = statdb_parse_mode(thisline);

		/* Move to the next bit */
		thisline = ptr + 1;
		if (thisline >= loaded_list_end)
			ohshit(_("unexpected end of line in statoverride file"));

		fnn = findnamenode(thisline, 0);
		if (fnn->statoverride)
			ohshit(_("multiple statusoverides present for file '%.250s'"),
			       thisline);
		fnn->statoverride = fso;

		/* Moving on.. */
		thisline = nextline;
	}

	onerr_abort--;
}


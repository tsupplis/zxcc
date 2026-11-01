/*

	CPMREDIR: CP/M filesystem redirector
	Optional Open file tracker 
	Copyright (C) 2021, Mark Ogden <mark.pm.ogden@btinternet.com>

	This is an addition to the CPMREDIR
	Copyright (C) 1998, John Elliott <jce@seasip.demon.co.uk>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Library General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Library General Public License for more details.

	You should have received a copy of the GNU Library General Public
	License along with this library; if not, write to the Free
	Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
#include "cpmint.h"

/* CP/M does not require that files opened for reading need to be closed,
 * this has two impacts
 * 1) a lot of file handles can remain opened even when the file is no
 *    longer used. For modern OS builds this isn't a major problem as
 *    the system limits are quite high. It is however wasteful
 * 2) for windows it can lead to issues when trying to delete / rename a file
 *    as normally windows will not allow files to be deleted/renamed if the
 *    file is currently open. Unix variants don't have this restriction.
 *
 * As an example the build of cgen.com using my decompiled sources
 * the linq phase without tracking left 42 open files
 * with tracking this was reduced to 2
 *  
 * This code keeps track of files that are opened until they are explicitly
 * closed or the FCB used to open the file is reused, or the file needs to be
 * renamed or deleted.
 * To do this it keeps track of the expanded filename, fcb location and
 * allocated file handle
 * 
 * Two public functions are used to manage the file list, and are called from
 * within the bdos emulation
 * 
 * trackFile(char *fname, void *fcb, int fd)
 *	removes existing tracking with matchin fcb or fd and
 *	if (fname != NULL) - add the info to the head of the open files list
 *	it returns fd
 * 
 * the function is called in the following circumstances
 * 1) before closing a file (fname is NULL)
 * 2) just after the file has been  opened/created. 
 * 3) to remove association with a given fcb trackFile(NULL, fcb, -1)
 *
 * note a helper macro releaseFCB(fcb) can be used for (3) above
 * 
 * releaseFile(char *fname)
 *  this scans through the list of open files and for each open file
 *  with a matching fname, the file is closed
 * 
 * the function is called before deleting a file or renaming a file
 * 
 * 
 * there is a helper function that removes the info from the list
 * 
 * Notes:
 * For most applications the tracker could in principle automatically
 * close existing open files at the start of a new executable invocation.
 * Unfortunately this does not support the case where there is a scripting
 * engine intercepting the warm reboots, as it may need to keep the script
 * source file open.
 * 
 * Note in theory it would be possible for a CP/M program to open a file
 * with a given fcb, move the fcb internally and then open another file
 * with the original fcb. If this happens the FCB tracking could cause
 * a problem. I am not aware of any real programs that do this.
 * Please let me know if the situation arises.
*/

#ifdef FILETRACKER

typedef struct _track {
	struct _track* next;
	int handle;
	void* fcb;
	char* fname;
	dword token;
} track_t;

static track_t* openFiles;
static dword nextToken = 1;

static track_t* rmHandle(track_t* s) {
	track_t* next = s->next;
	cpm_byte* fcb = s->fcb;

	if (fcb[16] == 0xFD && fcb[17] == 0 &&
		redir_rd32(fcb + TRACK_OFFSET) == s->token) {
		fcb[16] = fcb[17] = 0;
		redir_wr32(fcb + TRACK_OFFSET, 0);
	}
	free(s->fname);
	free(s);
	return next;
}

int isTrackedFile(cpm_byte* fcb, int fd) {
	track_t* s;
	dword token = redir_rd32(fcb + TRACK_OFFSET);

	for (s = openFiles; s; s = s->next)
		if (s->handle == fd && s->token == token)
			return 1;
	return 0;
}

#endif

#ifdef DEBUG
void show_fcb_Msg(cpm_byte* fcb);
#endif

void releaseFile(char* fname) {
#ifdef FILETRACKER
	track_t** link = &openFiles;
	while (*link)
		if (strcmp((*link)->fname, fname) == 0) {
			close((*link)->handle);
			*link = rmHandle(*link);
		}
		else
			link = &(*link)->next;
#endif
}


int trackFile(char* fname, void* fcb, int fd) {
#ifdef FILETRACKER
	track_t** link = &openFiles;
	track_t* s;
	cpm_byte* cfcb = fcb;

	while (*link) {	/* find any existing fcb or fd */
		if ((*link)->fcb == fcb || (*link)->handle == fd) {
			if ((*link)->handle != fd)
				close((*link)->handle);
			*link = rmHandle(*link);
		}
		else
			link = &(*link)->next;
	}
	if (fname && fd >= 0) {
		if (!nextToken) {
			fprintf(stderr, "cpmredir: File tracking IDs exhausted\n");
			exit(1);
		}
		if ((s = malloc(sizeof(track_t))) == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		s->next = openFiles;
		s->fname = strdup(fname);
		if (!s->fname) {
			free(s);
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		s->fcb = fcb;
		s->handle = fd;
		s->token = nextToken;
		nextToken = (nextToken + 1) & 0xFFFFFFFFUL;
		/* Copies of an FCB retain the ID, but a recycled host handle does not. */
		redir_wr32(cfcb + TRACK_OFFSET, s->token);
		openFiles = s;
	}
#endif

	return fd;
}

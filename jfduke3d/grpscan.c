#include "compat.h"
#include "baselayer.h"

#include "scriptfile.h"
#include "cache1d.h"
#include "crc32.h"

#include "duke3d.h"
#include "grpscan.h"
#include "dnAPI.h"

struct grpfile grpfiles[numgrpfiles] = {
	{ "Registered Version 1.3d",	0xBBC9CE44, 26524524, GAMEDUKE, NULL },
	{ "Registered Version 1.4",	0xF514A6AC, 44348015, GAMEDUKE, NULL },
	{ "Registered Version 1.5",	0xFD3DCFF1, 44356548, GAMEDUKE, NULL },
	{ "Shareware Version",		0x983AD923, 11035779, GAMEDUKE, NULL },
	{ "Mac Shareware Version",	0xC5F71561, 10444391, GAMEDUKE, NULL },
	{ "Mac Registered Version",     0x00000000, 0,        GAMEDUKE, NULL },
	{ "NAM",                        0x75C1F07B, 43448927, GAMENAM,  NULL },
};
struct grpfile *foundgrps = NULL;

#define GRPCACHEFILE "grpfiles.cache"
static struct grpcache {
	struct grpcache *next;
	char name[BMAX_PATH+1];
	int size;
	int mtime;
	int crcval;
} *grpcache = NULL, *usedgrpcache = NULL;

//#define USE_GRPCACHE
#ifdef USE_GRPCACHE
static int LoadGroupsCache(void)
{
	struct grpcache *fg;

	int fsize, fmtime, fcrcval;
	char *fname;

	scriptfile *script;

	script = scriptfile_fromfile(GRPCACHEFILE);
	if (!script) return -1;

	while (!scriptfile_eof(script)) {
		if (scriptfile_getstring(script, &fname)) break;	// filename
		if (scriptfile_getnumber(script, &fsize)) break;	// filesize
		if (scriptfile_getnumber(script, &fmtime)) break;	// modification time
		if (scriptfile_getnumber(script, &fcrcval)) break;	// crc checksum

		fg = calloc(1, sizeof(struct grpcache));
		fg->next = grpcache;
		grpcache = fg;

		strncpy(fg->name, fname, BMAX_PATH);
		fg->size = fsize;
		fg->mtime = fmtime;
		fg->crcval = fcrcval;
	}

	scriptfile_close(script);
	return 0;
}
#else
static int LoadGroupsCache(void) {
	return -1;
}
#endif

static void FreeGroupsCache(void)
{
	struct grpcache *fg;

	while (grpcache) {
		fg = grpcache->next;
		free(grpcache);
		grpcache = fg;
	}
}

int ScanGroups(void)
{
	CACHE1D_FIND_REC *srch, *sidx;
	struct grpcache *fg, *fgg;
	struct grpfile *grp;
	char *fn;
	struct Bstat st;

	initprintf("Scanning for GRP files...\n");
	
	LoadGroupsCache();

	srch = klistpath("/", dnGetGRPName(), CACHE1D_FIND_FILE);

	for (sidx = srch; sidx; sidx = sidx->next) {
		for (fg = grpcache; fg; fg = fg->next) {
			if (!Bstrcmp(fg->name, sidx->name)) break;
		}

		if (fg) {
			if (findfrompath(sidx->name, &fn)) continue;	// failed to resolve the filename
			if (Bstat(fn, &st)) { free(fn); continue; }	// failed to stat the file
			free(fn);
			if (fg->size == st.st_size && fg->mtime == st.st_mtime) {
				grp = (struct grpfile *)calloc(1, sizeof(struct grpfile));
				grp->name = strdup(sidx->name);
				grp->crcval = fg->crcval;
				grp->size = fg->size;
				grp->next = foundgrps;
				foundgrps = grp;

				fgg = (struct grpcache *)calloc(1, sizeof(struct grpcache));
				strcpy(fgg->name, fg->name);
				fgg->size = fg->size;
				fgg->mtime = fg->mtime;
				fgg->crcval = fg->crcval;
				fgg->next = usedgrpcache;
				usedgrpcache = fgg;
				continue;
			}
		}

		{
			int b, fh;
			int crcval;
			char buf[16*512];

			fh = openfrompath(sidx->name, BO_RDONLY|BO_BINARY, BS_IREAD);
			if (fh < 0) continue;
			if (fstat(fh, &st)) continue;

			initprintf(" Checksumming %s...", sidx->name);
			crc32init((unsigned long *)&crcval);			
			do {
				b = read(fh, buf, sizeof(buf));
				if (b > 0) crc32block((unsigned long *)&crcval, buf, b);
			} while (b == sizeof(buf));
			crc32finish((unsigned long *)&crcval);
			close(fh);
			initprintf(" Done\n");
		
			grp = (struct grpfile *)calloc(1, sizeof(struct grpfile));
			grp->name = strdup(sidx->name);
			grp->crcval = crcval;
			grp->size = st.st_size;
			grp->next = foundgrps;
			foundgrps = grp;

			fgg = (struct grpcache *)calloc(1, sizeof(struct grpcache));
			strncpy(fgg->name, sidx->name, BMAX_PATH);
			fgg->size = st.st_size;
			fgg->mtime = st.st_mtime;
			fgg->crcval = crcval;
			fgg->next = usedgrpcache;
			usedgrpcache = fgg;
		}
	}

	klistfree(srch);
	FreeGroupsCache();

	if (usedgrpcache) {
		FILE *fp;
		fp = fopen(GRPCACHEFILE, "wt");
		if (fp) {
			for (fg = usedgrpcache; fg; fg=fgg) {
				fgg = fg->next;
				fprintf(fp, "\"%s\" %d %d %d\n", fg->name, fg->size, fg->mtime, fg->crcval);
				free(fg);
			}
			fclose(fp);
		}
	}

	return 0;
}

void FreeGroups(void)
{
	struct grpfile *fg;

	while (foundgrps) {
		fg = foundgrps->next;
		free((char*)foundgrps->name);
		free(foundgrps);
		foundgrps = fg;
	}
}


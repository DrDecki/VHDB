#include "vhdb_promote.h"
#include "vhdb_vitainstall.h"

#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include <string.h>

#define LOG_PATH VHDB_DATA_DIR "/update_result.txt"

static void write_log(const char *line)
{
	SceUID file = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
				0777);

	if (file < 0)
		return;
	sceIoWrite(file, line, strlen(line));
	sceIoClose(file);
}

int main(void)
{
	SceIoStat stat;

	if (sceIoGetstat(VHDB_STAGED_UPDATE, &stat) >= 0) {
		if (vhdb_promote_directory(VHDB_STAGED_UPDATE))
			write_log("ok");
		else
			write_log(vhdb_promote_error());
	}

	vhdb_remove_tree(VHDB_STAGED_UPDATE);

	sceAppMgrLoadExec("ux0:app/VHDB00001/eboot.bin", NULL, NULL);

	sceKernelExitProcess(0);
	return 0;
}

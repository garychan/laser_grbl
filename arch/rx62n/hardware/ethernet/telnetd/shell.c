/*
 * Copyright (c) 2003, Adam Dunkels.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file is part of the uIP TCP/IP stack.
 *
 * $Id: shell.c,v 1.1 2006/06/07 09:43:54 adam Exp $
 *
 */

#include "shell.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "p5q.h"
#include "fatfs/ff.h"
#include "audio.h"
#include "net/uip.h"

#include "FreeRTOS.h"
#include "task.h"

extern void start_grbl_task();
extern xTaskHandle grbl_handle;
extern void serial_receive(char*str);
extern FATFS Fatfs;

struct ptentry {
    char *commandstr;
    void (* pfunc)(char *str);
    int num_args;
};

#define SHELL_PROMPT "grbl$ "
char tmp[1024]; //var used by shell_printf() etc... to not overuse the stack..

static int grbl_running = 0;

void shell_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp) - 1, fmt, ap);
    va_end(ap);

    shell_send_str(tmp);
}

/*---------------------------------------------------------------------------*/
static void parse(register char *str, struct ptentry *t)
{
    struct ptentry *p;
    for(p = t; p->commandstr != NULL; ++p) {
	if(strncmp(p->commandstr, str, strlen(p->commandstr)) == 0) {
	    break;
	}
    }

    if (p->num_args)
    {
	str = strchr(str, ' ');
	if (str != NULL)
	{
	    str++; // skip the space
	}
	else
	{
	    shell_printf("Need %d arguments", p->num_args);
	}
    }
    p->pfunc(str);
}
/*---------------------------------------------------------------------------*/
static void help(char *str)
{
    shell_output("Available commands:", "");
    shell_output("stats      - show network statistics", "");
    shell_output("grbl       - start grbl", "");
    shell_output("reset_grbl - reset the grbl task", "");
    shell_output("help, ?    - show help", "");
    shell_output("exit       - exit shell", "");
}
/*---------------------------------------------------------------------------*/
static void unknown(char *str)
{
    if(strlen(str) > 0) {
	shell_output("Unknown command: ", str);
    }
}

static void mkfs(char *str)
{
    FRESULT result  = f_mkfs (0, 1, 1024);
    sprintf(tmp,"result %d", result);
    shell_output(tmp, "");
}

static void ls(char * str)
{
    DIR dir;
    FILINFO fno;
    char *fn;
    FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;


#if _USE_LFN
    static char lfn[_MAX_LFN * + 1];
    fno.lfname = lfn;
    fno.lfsize = sizeof(lfn);
#endif

    FRESULT result = f_opendir (&dir, "");
    if (result != FR_OK)
    {
	shell_printf("Opendir failed %x", result);
	return;
    }

    for (;;) {
	result = f_readdir(&dir, &fno);
	if (result != FR_OK || fno.fname[0] == 0) break;
	if (fno.fname[0] == '.') continue;
#if _USE_LFN
	fn = *fno.lfname ? fno.lfname : fno.fname;
#else
	fn = fno.fname;
#endif
	shell_printf("%s%s", fn, (fno.fattrib & AM_DIR) ? "/" : "");
    }

    /* Get volume information and free clusters of drive 1 */
    result = f_getfree("0:", &fre_clust, &fs);
    if (result)
    {
	shell_printf("getfree failed %x", result);
	return;
    }

    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;

    /* Print free space in unit of KB (assuming 512 bytes/sector) */
    shell_printf("%lu KB total.\n"
		 "%lu KB available.\n",
		 tot_sect / 2, fre_sect / 2);

}

static void mkdir(char * str)
{
    FRESULT result  = f_mkdir(str);
    if (result != FR_OK)
    {
	shell_printf("mkdir failed %x", result);
    }
}

static void cd(char *str)
{
    FRESULT result = f_chdir(str);

    shell_printf("change to %s", str);

    if (result != FR_OK)
    {
	shell_printf("chdir failed %x", result);
    }    
}

char buffer[80];

static void pwd(char *str)
{
    FRESULT result  = f_getcwd(buffer, sizeof(buffer));
    if (result != FR_OK)
    {
	shell_printf("pwd failed %x", result);
	return;
    }
    shell_printf("%s", buffer);
}


static void cat(char *str)
{
    FIL File1;
    FRESULT result;
    char *name = str;
    int flags = FA_READ;

    if (name[0] == '>')
    {
	name++;
	flags = FA_CREATE_ALWAYS | FA_WRITE;
	if (name[0] == '>')
	{
	    name++;
	    flags = FA_OPEN_ALWAYS | FA_WRITE;
	}

	while (*name++ == ' '); // skip the whitespace
    }

    result = f_open(&File1, name, flags);
    if (result != FR_OK)
    {
	shell_printf("Open failed %x", result);
	return;	
    }
    
    if (flags & FA_OPEN_ALWAYS)
    {
	result = f_lseek(&File1, File1.fsize);
	if (result != FR_OK)
	{
	    shell_printf("Seek failed %x\n", result);
	    f_close(&File1);
	    return;
	}
    }

    if (flags & FA_WRITE)
    {

    }
    else
    {
	while (f_gets (buffer, sizeof(buffer), &File1) != NULL)
	{
	    shell_printf("%s\n", buffer);
	}
    }

    f_close(&File1);    
}

static void beep(char *str)
{
    audio_beep(atoi(str), 300);
}

static void ps(char *str)
{
    extern void vTaskList( signed char *pcWriteBuffer );
    extern char *pcGetTaskStatusMessage( void );

	shell_printf("Tasks are reported as blocked ('B'), ready ('R'), deleted ('D') or suspended ('S')\n");
    vTaskList((signed char *) tmp );
	shell_send_str(tmp);
}

static void start_grbl(char *str)
{
	shell_printf("Starting grbl... type exit to return to shell\n");
	grbl_running = 1;
	vTaskResume(grbl_handle);
}

static void reset_grbl(char *str)
{
	shell_printf("reseting grbl... type \"grbl\" to start it again\n");
	grbl_running = 0;
	//vTaskDelete(grbl_handle);
	//start_grbl_task();
}

static void rm(char *str)
{
    FRESULT result = f_unlink(str);
    if (result != FR_OK)
    {
	shell_printf("Failed %d\n", result);
    }
}

/*---------------------------------------------------------------------------*/
static struct ptentry parsetab[] =
{
    {"stats",    help,         0},
    {"grbl",     start_grbl,   0},
    {"reset_grbl",reset_grbl,  0},
    {"help",     help,         0},
    {"mkfs",     mkfs,         0},
    {"mkdir",    mkdir,        1},
    {"cd",       cd,           1},
    {"cat",      cat,          1},
    {"pwd",      pwd,          0},
    {"ls",       ls,           0},
    {"rm",       rm,           1},
    {"ps",       ps,           0},
    {"beep",     beep,         1},
    {"exit",     shell_quit,   0},
    {"?",        help},
    {NULL, unknown}
};
/*---------------------------------------------------------------------------*/
void shell_init(void)
{
}
/*---------------------------------------------------------------------------*/
void shell_start(void)
{
    shell_output("uIP command shell", "");
    shell_output("Type '?' and return for help", "");
    shell_prompt(SHELL_PROMPT);
}
/*---------------------------------------------------------------------------*/
void shell_input(char *cmd)
{
	if(grbl_running) {
		if(!strcmp(cmd,"exit")) {
			shell_printf("closing grbl.\n");
			vTaskSuspend(grbl_handle);
    			shell_prompt(SHELL_PROMPT);
			grbl_running = 0;
			return;
		}
		serial_receive(cmd);
		return;
	}
    parse(cmd, parsetab);
    shell_prompt(SHELL_PROMPT);
}
/*---------------------------------------------------------------------------*/

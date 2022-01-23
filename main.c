#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <libcdvd.h>
#include <debug.h>
#include <malloc.h>
#include <string.h>
#include <osd_config.h>

#include <kernel.h>
#include <fileio.h>
#include <sifrpc.h>

#include <loadfile.h>

#include "cnf_lite.h"

/* Some macros used for patching. */
#define JAL(addr)	(0x0c000000|(0x3ffffff&((addr)>>2)))
#define JMP(addr)	(0x08000000|(0x3ffffff&((addr)>>2)))
#define GETJADDR(addr)	((addr&0x03FFFFFF)<<2)

enum CONSOLE_REGIONS{
	CONSOLE_REGION_INVALID	= -1,
	CONSOLE_REGION_JAPAN	= 0,
	CONSOLE_REGION_USA,	//USA and HK/SG.
	CONSOLE_REGION_EUROPE,
	CONSOLE_REGION_CHINA,

	CONSOLE_REGION_COUNT
};

enum DISC_REGIONS{
	DISC_REGION_INVALID	= -1,
	DISC_REGION_JAPAN	= 0,
	DISC_REGION_USA,
	DISC_REGION_EUROPE,

	DISC_REGION_COUNT
};

static char ver[64], cdboot_path[256], romver[16];
static unsigned char SelectedVMode;
static void (*MainEPC)(void);
static char ConsoleRegion=CONSOLE_REGION_INVALID, DiscRegion=DISC_REGION_INVALID;

static u32 *ScanForPattern(u32 *start, u32 range, const u32 *pattern, u32 PatternSize, const u32 *mask)
{
	u32 i, *result, PatternOffset;

	for(result=NULL,i=0; i<range; i+=4)
	{
		for(PatternOffset=0; PatternOffset<PatternSize; PatternOffset+=4)
		{
			if((start[(i+PatternOffset)/4]&mask[PatternOffset/4])!=pattern[PatternOffset/4]) break;
		}

		if(PatternOffset==PatternSize){	//Pattern found.
			result=&start[i/4];
			break;
		}
	}

	return result;
}

static u64 *emu_SYNCHV_I=(u64*)0x70001920;
static u64 *emu_SYNCHV_NI=(u64*)0x70001928;

static int InitVideoModeParams(void)
{	//Replacement function. Overrides the SYCHV parameters set by the driver.
	u64 temp;

	temp=0;	//It wasn't initialized in the Sony originals, but I hated the compiler warning.
	if(SelectedVMode==3){	//PAL
		*emu_SYNCHV_I=0x00A9000502101401;
		*emu_SYNCHV_NI=0x00A9000502101404;
	}
	else{	//NTSC
		temp=(temp&0xFFFFFC00)|3;
		temp=(temp&0xFFF003FF)|0x1800;
		temp=(temp&0xC00FFFFF)|0x01200000;
		temp=(temp&0xFFFFFC00FFFFFFFF)|(0xC000L<<19);
		temp=(temp&0xFFE003FFFFFFFFFF)|(0xF300L<<35);
		temp=(temp&0x801FFFFFFFFFFFFF)|(0xC000L<<40);
		*emu_SYNCHV_I=temp;

		//Nearly a repeat of the block above.
		temp=(temp&0xFFFFFC00)|4;
		temp=(temp&0xFFF003FF)|0x1800;
		temp=(temp&0xC00FFFFF)|0x01200000;
		temp=(temp&0xFFFFFC00FFFFFFFF)|(0xC000L<<19);
		temp=(temp&0xFFE003FFFFFFFFFF)|(0xF300L<<35);
		temp=(temp&0x801FFFFFFFFFFFFF)|(0xC000L<<40);
		*emu_SYNCHV_NI=temp;
	}

	return 0;
}

static void SetPS1DrvVideoMode(void *start, int region, int VideoMode)
{
	u32 *PatchLoc, *RetAddr;
	//For NTSC consoles.
	const static u32 VModeSetPatternNTSC[]={
		0x24040000,	//addiu a0, zero, VideoMode
		0x00661825,	//or v1, v1, a2
		0xaf840000,	//sw a0, VideoModeParam(gp)
	};
	const static u32 VModeSetPatternMaskNTSC[]={
		0xFFFF0000,
		0xFFFFFFFF,
		0xFFFF0000
	};
	//For PAL consoles.
	const static u32 VModeSetPatternPAL[]={
		0x24030000,	//addiu v1, zero, VideoMode
		0x3c0400a9,	//lui a0, $00a9
		0x34840005,	//ori a0, a0, $0005
	};
	const static u32 VModeSetPatternMaskPAL[]={
		0xFFFF0000,
		0xFFFFFFFF,
		0xFFFFFFFF
	};
	const static u32 VModeSetRetPattern[]={
		0x03e00008	//jr ra
	};
	const static u32 VModeSetRetPatternMask[]={
		0xFFFFFFFF
	};
	const u32 *pattern, *mask;
	u32 PatternSize;

	if(region==CONSOLE_REGION_EUROPE){
		pattern=VModeSetPatternPAL;
		mask=VModeSetPatternMaskPAL;
		PatternSize=sizeof(VModeSetPatternPAL);
	}
	else{
		pattern=VModeSetPatternNTSC;
		mask=VModeSetPatternMaskNTSC;
		PatternSize=sizeof(VModeSetPatternNTSC);
	}

	if((PatchLoc=ScanForPattern(start, 0x20000, pattern, PatternSize, mask))!=NULL && (RetAddr=ScanForPattern(PatchLoc, 0x80, VModeSetRetPattern, sizeof(VModeSetRetPattern), VModeSetRetPatternMask))!=NULL){
		*PatchLoc=((*PatchLoc)&0xFFFF0000)|(VideoMode&3);
		*RetAddr=JMP((u32)&InitVideoModeParams);
	}
	else printf("SetPS1DrvVideoMode: error - pattern not found.\n");
}

static void ChangePS1DrvVideoMode(void){
	SetPS1DrvVideoMode((void*)0x00200000, ConsoleRegion, SelectedVMode);

	FlushCache(0);
	FlushCache(2);
}

static void SetROMVERRegion(void *start, char region){
	u32 *PatchLoc;
	const static u32 ROMVERParserPattern[]={
		0x93a20004,	//lbu v0, $0004(sp)
		0x24030043,	//addiu v1, zero, $0043
		0x00021600,	//sll v0, v0, 24
		0x00022603	//sra v0, v0, 24
	};
	const static u32 ROMVERParserMask[]={
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF
	};

	printf("SetROMVERRegion: Region: %c\n", region);

	if((PatchLoc=ScanForPattern(start, 0x20000, ROMVERParserPattern, sizeof(ROMVERParserPattern), ROMVERParserMask))!=NULL){
		*PatchLoc=0x24020000|region;	//li $v0, region
	}
	else printf("SetROMVERRegion: error - pattern not found.\n");
}

static void ChangeROMVERRegion(void)
{	/*	Cause the new PS1DRV program to "see" the desired region, instead of the letter in ROMVER.
		This will also change the compatibility list used. */
	char region;

	switch(DiscRegion)
	{
		case DISC_REGION_USA:
			region = 'A';
			break;
		case DISC_REGION_EUROPE:
			region = 'E';
			break;
		default:
			region = 'J';
	}
	SetROMVERRegion((void*)0x00200000, region);

	FlushCache(0);
	FlushCache(2);
}

static int HookUnpackFunction(void *start)
{
	u32 *PatchLoc;
	int result;
	const static u32 UnpackFuncPattern[]={	//FlushCache(0); FlushCache(2);
		0x24030064,	//addiu v1, zero, $0064
		0x0000202d,	//daddu a0, zero, zero
		0x0000000c,	//syscall (00000)
		0x24030064,	//addiu v1, zero, $0064
		0x24040002,	//addiu a0, zero, $0002
		0x0000000c	//syscall (00000)
	};
	const static u32 UnpackFuncPatternMask[]={
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF
	};

	if((PatchLoc=ScanForPattern(start, 0x1000, UnpackFuncPattern, sizeof(UnpackFuncPattern), UnpackFuncPatternMask))!=NULL)
	{
		PatchLoc[0]=0x3c020000|((u32)&ChangeROMVERRegion)>>16;		//lui $v0, &ChangeROMVERRegion
		PatchLoc[1]=0x34420000|(((u32)&ChangeROMVERRegion)&0xFFFF);	//ori $v0, &ChangeROMVERRegion
		PatchLoc[2]=0x0040f809;							//jalr $v0
		PatchLoc[3]=0;								//nop
		PatchLoc[4]=0;								//nop
		PatchLoc[5]=0;								//nop
		result=0;

		FlushCache(0);
		FlushCache(2);
	}
	else
	{
		printf("HookUnpackFunction: pattern not found. Not hooking.\n");
		result=1;
	}

	return result;
}

static unsigned short int GetBootROMVersion(void)
{
	int fd;
	char VerStr[5];

	fd=open("rom0:ROMVER", O_RDONLY);
	read(fd, romver, sizeof(romver));
	close(fd);
	VerStr[0]=romver[0];
	VerStr[1]=romver[1];
	VerStr[2]=romver[2];
	VerStr[3]=romver[3];
	VerStr[4]='\0';

	return((unsigned short int)strtoul(VerStr, NULL, 16));
}

static int GetConsoleRegion(void)
{
	int result;

	if((result=ConsoleRegion)<0){
		switch(romver[4]){
			case 'C':
				ConsoleRegion=CONSOLE_REGION_CHINA;
				break;
			case 'E':
				ConsoleRegion=CONSOLE_REGION_EUROPE;
				break;
			case 'H':
			case 'A':
				ConsoleRegion=CONSOLE_REGION_USA;
				break;
			case 'J':
				ConsoleRegion=CONSOLE_REGION_JAPAN;
		}

		result=ConsoleRegion;
	}

	return result;
}

static int HasValidDiscInserted(int mode)
{
	int DiscType, result;

	if(sceCdDiskReady(mode)==SCECdComplete){
		switch((DiscType=sceCdGetDiskType())){
			case SCECdDETCT:
			case SCECdDETCTCD:
			case SCECdDETCTDVDS:
			case SCECdDETCTDVDD:
				result=0;
				break;
			case SCECdUNKNOWN:
			case SCECdPSCD:
			case SCECdPSCDDA:
			case SCECdPS2CD:
			case SCECdPS2CDDA:
			case SCECdPS2DVD:
			case SCECdCDDA:
			case SCECdDVDV:
			case SCECdIllegalMedia:
				result=DiscType;
				break;
			default:
				result=-1;
		}
	}
	else{
		result=-1;
	}

	return result;
}

static void DelayIO(void){
	int i;

	for(i=0; i<24; i++) nopdelay();
}

static void ClearStatusLine(void){
	scr_setXY(0, 3);
	scr_printf("                                                       ");
	scr_setXY(0, 3);
}

static void InvokeSetGsCrt(void)
{	/*	This is necessary because PS1DRV runs off the initial state that LoadExecPS2 leaves the console in.
		Therefore it is necessary to invoke SetGsCrt with the desired video mode,
		so that the registers that aren't set by PS1DRV will be in the correct state.

		For example, the PLL mode (54.0MHz for NTSC or 53.9MHz for PAL, for applicable console models that have a PLL setting)
			is only set within SetGsCrt, but not in PS1DRV. */
	SetGsCrt(1, SelectedVMode, 1);
	MainEPC();
}

static int GetDiscRegion(const char *path)
{
	int region;
	const char *name;

	region = DISC_REGION_INVALID;
	if(((name = strrchr(path, '/')) != NULL) || ((name = strrchr(path, '\\')) != NULL) || ((name = strrchr(path, ':')) != NULL))
	{	//Filename found.
		++name;
		if(strlen(name) >= 3)
		{	/*	This may not be the best way to do it, but it seems like the 3rd letter typically indicates the region:
				SxPx - Japan
				SxUx - USA
				SxEx - Europe	*/
			switch(name[2])
			{
				case 'P':
					region = DISC_REGION_JAPAN;
					break;
				case 'U':
					region = DISC_REGION_USA;
					break;
				case 'E':
					region = DISC_REGION_EUROPE;
					break;
			}
		}
	}

	return region;
}

static void InitializeUserMemory(void *start, void *end)
{
	u8 *ptr;

	//clear memory.
	for (ptr = (u8*)start; ptr < (u8*)end; ptr += 64) {
		asm (
			"\tsq $0, 0(%0) \n"
			"\tsq $0, 16(%0) \n"
			"\tsq $0, 32(%0) \n"
			"\tsq $0, 48(%0) \n"
			:: "r" (ptr)
		);
	}

	FlushCache(0);
	FlushCache(2);
}

int main(int argc, char *argv[])
{
	char *args[3];
	t_ExecData ElfData;
	int DiscType, done;
	unsigned short int ROMVersion;

	SifInitRpc(0);
	fioInit();
	sceCdInit(SCECdINoD);

	//Erase the region where the packed ELF was loaded to.
	InitializeUserMemory((void*)0x00100000, (void*)0x00200000);

	init_scr();
	scr_printf(	"PS1 Video Mode Negator v1.10\n"
			"============================\n\n");

	done=0;
	do{
		ClearStatusLine();

		if((DiscType=HasValidDiscInserted(1))>=0){
			if(DiscType==0){
				scr_printf("Reading disc...");
				while(HasValidDiscInserted(1)==0){DelayIO();};
			}
			else{
				switch(DiscType){
					case SCECdPSCD:
					case SCECdPSCDDA:
						done = 1;
						break;
					default:
						scr_printf("Unsupported disc. Please insert a PlayStation game disc.");
						sceCdStop();
						sceCdSync(0);
						while(HasValidDiscInserted(1)>0){DelayIO();};
				}
			}
		}else{
			scr_printf("No disc inserted. Please insert a PlayStation game disc.");
			while(HasValidDiscInserted(1)<0){DelayIO();};
		}
	}while(!done);

	ClearStatusLine();
	scr_printf("Reading disc...");

	sceCdDiskReady(0);
	DiscType=sceCdGetDiskType();

	// Double check PS1 disc type & read CNF
	if (((DiscType==SCECdPSCD) || (DiscType==SCECdPSCDDA)) && (Read_SYSTEM_CNF(cdboot_path, ver) == 1))
	{
		args[0] = "rom0:PS1DRV";
		//These arguments aren't required for running PS1DRV, but provide them because OSDSYS does.
		args[1] = cdboot_path;  // 1st arg is main elf path
		args[2] = ver;          // 2nd arg is ver

		DiscRegion = GetDiscRegion(cdboot_path);
		ROMVersion = GetBootROMVersion();
		GetConsoleRegion();

		//Determine the video mode to use. If the disc's region can be determined, use the disc's region.
		if(DiscRegion != DISC_REGION_INVALID)
		{	//Select video mode based on the disc's region (Europe = PAL game).
			SelectedVMode=(DiscRegion==DISC_REGION_EUROPE)?3:2;
		}
		else
		{	//Negate video mode: if region == Europe, set video mode to NTSC. Otherwise, PAL.
			SelectedVMode=(ConsoleRegion==CONSOLE_REGION_EUROPE)?2:3;
			//Since the disc's region cannot be determined, make an assumption based on the selected video mode.
			DiscRegion = SelectedVMode == 3 ? 'E' : 'J';

			printf("Assuming disc region.\n");
		}

		printf("Console: %d, disc region: %d, video mode: %d\n", ConsoleRegion, DiscRegion, SelectedVMode);

		FlushCache(0);

		if(SifLoadElf("rom0:PS1DRV", &ElfData)==0)
		{
			if(ROMVersion>0x200)
			{	/*	For ROMs after v2.00 (v2.20 is the earliest known version), PS1DRV was upgraded to v1.3.0 and is packed.
					They are also universal, which includes the game compatibility lists for all regions.
					The region is determined with the ROMVER string. */
				HookUnpackFunction((void*)ElfData.epc);
			}
			else
			{	//Older ROMs have unpacked PS1DRV programs that are coded for either NTSC or PAL.
				ChangePS1DrvVideoMode();
			}

			fioExit();
			SifExitRpc();

			MainEPC = (void*)ElfData.epc;

			FlushCache(0);
			FlushCache(2);

			ExecPS2(&InvokeSetGsCrt, (void*)ElfData.gp, 3, args);	//Won't return from here.
		}
	}

	fioExit();
	sceCdInit(SCECdEXIT);
	SifExitRpc();

	return 0;
}

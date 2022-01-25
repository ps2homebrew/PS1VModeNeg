# PS1Drv Video Mode Negater (PS1VModeNeg)

## Changelog

    Update 2017/08/26: v1.10 released.
    Update 2014/07/21: v1.01 released.
    Update 2014/05/13: v1.00 released.
    Update 2013/08/31: Initial public release

## Introduction

The PlayStation driver video mode negater (PS1VModeNeg) is a tool that selects the correct video mode for your imported PlayStation game, regardless of what your PlayStation 2 console's PlayStation driver (PS1DRV) has been fixed to use.

Depending on the boot ROM (aka "BIOS") of the console, one of these two different methods will be used:

1. Patch the video parameters initialization function.[^1]
2. Patch the ROMVER string parsing function (Fools the driver to think that your console is from another region).[^2]

[^1]: is used for consoles belonging to the SCPH-70000 (ROM ≤ v2.00) series and older.

[^2]: is used for consoles belonging to the SCPH-75000 (ROM > v2.00) series and newer.

The PlayStation drivers of the SCPH-75000 appear to have been coded to be universal, like with the rest of the design of the slimline consoles in general. This means that full compatibility with import games should be achievable with minimal patching.

Older drivers are more difficult to tackle, as they have been hard-coded to work with games from your console's region. e.g. NTSC consoles appear to have a built-in compatibility list and assume that your games will use the NTSC video mode. Code for supporting the video mode from the other region (e.g. NTSC games, for a PAL console) will have to be added.

It's probably also not too surprising if some games work well on only consoles native to the region that they are from due to the region-specific game compatibility list.

**Note:** On older consoles (SCPH-70000 and older), there is a risk that your TV might blow up or get damaged (worst case) if my code turns out to be faulty.

That hasn't happened to the two TVs I've tested on, but I don't want to guarantee anything because the method I use involves tweaking with the values that the PS1 driver appears to write to the GS's SYNCHV register (One of the undocumented registers that Sony had prohibited external developers from writing to directly).

The code I'm using to calculate the replacement values for SYNCHV has been taken from the PS1 driver of my SCPH-77006 (PS1DRV v1.3.0), and it should be perfectly safe if it was copied properly. However, accidents might happen and something such as mistakes such as typos could exist. :(

**Use this tool at your own risk!**

## Video Mode & Region Detection

There are now two methods used to determine the video mode to use:

1. Read SYSTEM.CNF from the disc and determine which region it is from. If the console is an SCPH-75000 or newer, the ROM region will be also changed to fit the disc. PS1DRV uses the ROM's region to select the appropriate game compatibility chart.
    * SxPx - Japan (NTSC)
    * SxEx - Europe (PAL)
    * SxUx - USA (NTSC)
2. If the region cannot be determined in (1), then the video mode selected will be the opposite of your console's. The game compatibility list will hence not apply to US games if this happens. If the console is an SCPH-75000 or newer, the ROM region will be changed to Japan (NTSC) or Europe (PAL).
    * NTSC console ⇒ PAL video mode
    * PAL console ⇒ NTSC video mode.

## Known limitations/bugs

* None

## Changelog for v1.10

1. Bugfix: Video mode not set completely.
2. Call SetGsCrt with the desired video mode before booting PS1DRV. 3. PS1DRV was made to run off the state that LoadExecPS2 leaves the console in, hence it does not entirely change the video mode on its own.
3. Updated to build with the modern PS2SDK revision.

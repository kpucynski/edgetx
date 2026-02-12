//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Created by a sound utility.
//	Kept as a sample, DOOM2 sounds.
//


#include <stdlib.h>


#include "doomtype.h"
#include "sounds.h"

//
// Information about all the music
//

#define MUSIC(name) \
    { name, 0, NULL, NULL }

musicinfo_t S_music[] =
{
    MUSIC(NULL),
};


//
// Information about all the sfx
//

#define SOUND(name, priority) \
  { NULL, name, priority, NULL, -1, -1, 0, 0, -1, NULL }
#define SOUND_LINK(name, priority, link_id, pitch, volume) \
  { NULL, name, priority, &S_sfx[link_id], pitch, volume, 0, 0, -1, NULL }

sfxinfo_t S_sfx[] =
{
  // S_sfx[0] needs to be a dummy for odd reasons.
  SOUND("none",   0),

  // sfx_pistol .. sfx_sawhit  (weapons)
  SOUND("pistol", 64),
  SOUND("shotgn", 64),
  SOUND("sgcock", 64),
  SOUND("dshtgn", 64),
  SOUND("dbopn",  64),
  SOUND("dbcls",  64),
  SOUND("dbload", 64),
  SOUND("plasma", 64),
  SOUND("bfg",    64),
  SOUND("sawup",  64),
  SOUND("sawidl", 118),
  SOUND("sawful", 64),
  SOUND("sawhit", 64),

  // sfx_rlaunc .. sfx_firxpl  (projectiles)
  SOUND("rlaunc", 64),
  SOUND("rxplod", 70),
  SOUND("firsht", 70),
  SOUND("firxpl", 70),

  // sfx_pstart .. sfx_swtchx  (doors, switches, platforms)
  SOUND("pstart", 100),
  SOUND("pstop",  100),
  SOUND("doropn", 100),
  SOUND("dorcls", 100),
  SOUND("stnmov", 119),
  SOUND("swtchn", 78),
  SOUND("swtchx", 78),

  // sfx_plpain .. sfx_pepain  (pain)
  SOUND("plpain", 96),
  SOUND("dmpain", 96),
  SOUND("popain", 96),
  SOUND("vipain", 96),
  SOUND("mnpain", 96),
  SOUND("pepain", 96),

  // sfx_slop .. sfx_telept  (misc)
  SOUND("slop",   78),
  SOUND("itemup", 78),
  SOUND("wpnup", 78),
  SOUND("oof",    96),
  SOUND("telept", 32),

  // sfx_posit1 .. sfx_posit3  (zombieman sight)
  SOUND("posit1", 98),
  SOUND("posit2", 98),
  SOUND("posit3", 98),

  // sfx_bgsit1 .. sfx_pesit  (monster sight)
  SOUND("bgsit1", 98),
  SOUND("bgsit2", 98),
  SOUND("sgtsit", 98),
  SOUND("cacsit", 98),
  SOUND("brssit", 94),
  SOUND("cybsit", 92),
  SOUND("spisit", 90),
  SOUND("bspsit", 90),
  SOUND("kntsit", 90),
  SOUND("vilsit", 90),
  SOUND("mansit", 90),
  SOUND("pesit",  90),

  // sfx_sklatk .. sfx_skeswg  (monster attacks)
  SOUND("sklatk", 70),
  SOUND("sgtatk", 70),
  SOUND("skepch", 70),
  SOUND("vilatk", 70),
  SOUND("claw",   70),
  SOUND("skeswg", 70),

  // sfx_pldeth .. sfx_pdiehi  (player death)
  SOUND("pldeth", 32),
  SOUND("pdiehi", 32),

  // sfx_podth1 .. sfx_skedth  (monster death)
  SOUND("podth1", 70),
  SOUND("podth2", 70),
  SOUND("podth3", 70),
  SOUND("bgdth1", 70),
  SOUND("bgdth2", 70),
  SOUND("sgtdth", 70),
  SOUND("cacdth", 70),
  SOUND("skldth", 70),
  SOUND("brsdth", 32),
  SOUND("cybdth", 32),
  SOUND("spidth", 32),
  SOUND("bspdth", 32),
  SOUND("vildth", 32),
  SOUND("kntdth", 32),
  SOUND("pedth",  32),
  SOUND("skedth", 32),

  // sfx_posact .. sfx_vilact  (monster active / walking)
  SOUND("posact", 120),
  SOUND("bgact",  120),
  SOUND("dmact",  120),
  SOUND("bspact", 100),
  SOUND("bspwlk", 100),
  SOUND("vilact", 100),

  // sfx_noway .. sfx_chgun  (misc)
  SOUND("noway",  78),
  SOUND("barexp", 60),
  SOUND("punch",  64),
  SOUND("hoof",   70),
  SOUND("metal",  70),
  SOUND_LINK("chgun", 64, sfx_pistol, 150, 0),

  // sfx_tink .. sfx_getpow
  SOUND("tink",   60),
  SOUND("bdopn",  100),
  SOUND("bdcls",  100),
  SOUND("itmbk",  100),
  SOUND("flame",  32),
  SOUND("flamst", 32),
  SOUND("getpow", 60),

  // sfx_bospit .. sfx_bosdth  (Icon of Sin)
  SOUND("bospit", 70),
  SOUND("boscub", 70),
  SOUND("bossit", 70),
  SOUND("bospn",  70),
  SOUND("bosdth", 70),

  // sfx_manatk .. sfx_mandth  (mancubus)
  SOUND("manatk", 70),
  SOUND("mandth", 70),

  // sfx_sssit .. sfx_ssdth  (wolfenstein SS)
  SOUND("sssit",  70),
  SOUND("ssdth",  70),

  // sfx_keenpn .. sfx_keendt  (keen)
  SOUND("keenpn", 70),
  SOUND("keendt", 70),

  // sfx_skeact .. sfx_skeatk  (revenant)
  SOUND("skeact", 70),
  SOUND("skesit", 70),
  SOUND("skeatk", 70),

  // sfx_radio
  SOUND("radio",  60),
};


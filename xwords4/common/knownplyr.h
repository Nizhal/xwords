/* -*- compile-command: "cd ../linux && make MEMDEBUG=TRUE -j3"; -*- */
/* 
 * Copyright 2020 by Eric House (xwords@eehouse.org).  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#ifndef _KNOWNPLYR_H_
#define _KNOWNPLYR_H_

#include "dutil.h"
#include "gameinfo.h"

/* XP_UCHAR** knpl_listPlayers( XW_DUtilCtxt* dctxt, uint_t* nNames ); */
/* void knpl_freePlayers( XW_DUtilCtxt* dctxt, XP_UCHAR** names ); */

# ifdef XWFEATURE_KNOWNPLAYERS
void kplr_cleanup( XW_DUtilCtxt* dutil );

XP_Bool kplr_havePlayers( XW_DUtilCtxt* dutil, XWEnv xwe );

void kplr_getPlayers( XW_DUtilCtxt* dutil, XWEnv xwe, const XP_UCHAR** players,
                      XP_U16* nFound );
XP_Bool kplr_getAddr( XW_DUtilCtxt* dutil, XWEnv xwe, const XP_UCHAR* name,
                      CommsAddrRec* addr );

XP_Bool kplr_addAddrs( XW_DUtilCtxt* dutil, XWEnv xwe, const CurGameInfo* gi,
                       CommsAddrRec addrs[], XP_U16 nAddrs );
# else
#  define kplr_cleanup( dutil )
# endif
#endif

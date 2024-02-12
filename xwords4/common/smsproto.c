/* -*- compile-command: "cd ../linux && make MEMDEBUG=TRUE -j3"; -*- */
/* 
 * Copyright 2018 by Eric House (xwords@eehouse.org).  All rights reserved.
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

#include <unistd.h>
#include <pthread.h>

#include "util.h"
#include "smsproto.h"
#include "comtypes.h"
#include "strutils.h"

#define MAX_WAIT 3
// # define MAX_MSG_LEN 50         /* for testing */
#ifndef MAX_LEN_BINARY
# define MAX_LEN_BINARY 115
#endif
/* PENDING: Might want to make SEND_NOW_SIZE smaller; might as well send now
   if even the smallest new message is likely to put us over. */
#define SEND_NOW_SIZE MAX_LEN_BINARY

/* To match the SMSService format */
#define SMS_PROTO_VERSION_JAVA 1
#define SMS_PROTO_VERSION_COMBO 2

#define PARTIALS_FORMAT 0

typedef struct _MsgRec {
    XP_U32 createSeconds;
    SMSMsgNet msgNet;
} MsgRec;

typedef struct _ToPhoneRec {
    XP_UCHAR phone[32];
    XP_U32 createSeconds;
    XP_U16 nMsgs;
    XP_U16 totalSize;
    MsgRec** msgs;
} ToPhoneRec;

typedef struct _MsgIDRec {
    int msgID;
    int count;
    struct {
        XP_U16 len;
        XP_U8* data;
    }* parts;
} MsgIDRec;

typedef struct _FromPhoneRec {
    XP_UCHAR phone[32];
    int nMsgIDs;
    MsgIDRec* msgIDRecs;
} FromPhoneRec;

struct SMSProto {
    XW_DUtilCtxt* dutil;
    pthread_t creator;
    pthread_mutex_t mutex;
    XP_U16 nNextID;
    int lastStoredSize;
    XP_U16 nToPhones;
    ToPhoneRec* toPhoneRecs;

    int nFromPhones;
    FromPhoneRec* fromPhoneRecs;
#ifdef DEBUG
    pthread_t starter;
    int nestCount;
#endif
    MPSLOT;
};

static int nextMsgID( SMSProto* state, XWEnv xwe );
static XWStreamCtxt* mkStream( SMSProto* state );
static void destroyStream( XWStreamCtxt* stream );
static SMSMsgArray* toNetMsgs( SMSProto* state, XWEnv xwe, ToPhoneRec* rec,
                               XP_Bool forceOld );
static ToPhoneRec* getForPhone( SMSProto* state, const XP_UCHAR* phone,
                                XP_Bool create );
static void addToOutRec( SMSProto* state, ToPhoneRec* rec, SMS_CMD cmd,
                         XP_U16 port, XP_U32 gameID, const XP_U8* buf,
                         XP_U16 buflen, XP_U32 nowSeconds );
static void addMessage( SMSProto* state, const XP_UCHAR* fromPhone, int msgID,
                        int indx, int count, const XP_U8* data, XP_U16 len );
static SMSMsgArray* completeMsgs( SMSProto* state, SMSMsgArray* arr,
                                  const XP_UCHAR* fromPhone, XP_U16 wantPort,
                                  int msgID );
static void savePartials( SMSProto* state, XWEnv xwe );
static void restorePartials( SMSProto* state, XWEnv xwe );
static void rmFromPhoneRec( SMSProto* state, int fromPhoneIndex );
static void freeMsgIDRec( SMSProto* state, MsgIDRec* rec, int fromPhoneIndex,
                          int msgIDIndex );
static void freeForPhone( SMSProto* state, const XP_UCHAR* phone );
static void freeMsg( SMSProto* state, MsgRec** msg );
static void freeRec( SMSProto* state, ToPhoneRec* rec );
#if defined DEBUG && defined COMMS_CHECKSUM
static void logResult( const SMSProto* state, XWEnv xwe,
                       const SMSMsgArray* result, const char* caller );
#else
# define logResult( state, xwe, result, caller )
#endif

SMSProto*
smsproto_init( MPFORMAL XWEnv xwe, XW_DUtilCtxt* dutil )
{
    SMSProto* state = (SMSProto*)XP_CALLOC( mpool, sizeof(*state) );
    pthread_mutex_init( &state->mutex, NULL );
    state->dutil = dutil;
    MPASSIGN( state->mpool, mpool );

    XP_U32 siz = sizeof(state->nNextID);
    const XP_UCHAR* keys[] = { KEY_NEXTID, NULL, };
    dutil_loadPtr( state->dutil, xwe, keys, &state->nNextID, &siz );
    XP_LOGF( "%s(): loaded nextMsgID: %d", __func__, state->nNextID );

    restorePartials( state, xwe );

    return state;
}

void
smsproto_free( SMSProto* state )
{
    if ( NULL != state ) {
        XP_ASSERT( state->creator == 0 || state->creator == pthread_self() );

        for ( XP_U16 ii = 0; ii < state->nToPhones; ++ii ) {
            freeRec( state, &state->toPhoneRecs[ii] );
        }
        XP_FREEP( state->mpool, &state->toPhoneRecs );

        if ( 0 < state->nFromPhones ) {
            XP_LOGF( "%s(): freeing undelivered partial messages", __func__ );
        }
        while (0 < state->nFromPhones) {
            FromPhoneRec* ffr = &state->fromPhoneRecs[0];
            while ( 0 < ffr->nMsgIDs ) {
                freeMsgIDRec( state, &ffr->msgIDRecs[0], 0, 0 );
            }
        }
        XP_ASSERT( !state->fromPhoneRecs ); /* above nulls this once empty */

        pthread_mutex_destroy( &state->mutex );

        XP_FREEP( state->mpool, &state );
    }
}

static void
headerToStream( XWStreamCtxt* stream, SMS_CMD cmd, XP_U16 port, XP_U32 gameID )
{
    // XP_LOGF( "%s(cmd: %d; gameID: %d)", __func__, cmd, gameID );
    stream_putU8( stream, SMS_PROTO_VERSION_JAVA );
    stream_putU16( stream, port );
    stream_putU8( stream, cmd );
    switch ( cmd ) {
    case NONE:
        XP_ASSERT(0);
        break;
    case INVITE:
        break;
    default:
        stream_putU32( stream, gameID );
    }
}

static XP_Bool
headerFromStream( XWStreamCtxt* stream, SMS_CMD* cmd, XP_U16* port, XP_U32* gameID )
{
    XP_Bool success = XP_FALSE;
    XP_U8 tmp;

    if ( stream_gotU8( stream, &tmp )
         && tmp == SMS_PROTO_VERSION_JAVA
         && stream_gotU16( stream, port )
         && stream_gotU8( stream, &tmp ) ) {
        *cmd = tmp;
        switch( *cmd ) {
        case INVITE:
            success = XP_TRUE;
            break;
        default:
            success = stream_gotU32( stream, gameID );
            break;
        }
    }
    // XP_LOGF( "%s() => cmd: %d; gameID: %d", __func__, *cmd, *gameID );
    return success;
}

/* Maintain a list of pending messages per phone number. When called and it's
 * been at least some amount of time since we last added something, or at
 * least some longer time since the oldest message was added, return an array
 * of messages ready to send via the device's raw SMS (i.e. respecting its
 * size limits.)

 * Pass in the current time, as that's easier than keeping an instance of
 * UtilCtxt around.
 */
SMSMsgArray*
smsproto_prepOutbound( SMSProto* state, XWEnv xwe, SMS_CMD cmd, XP_U32 gameID,
                       const void* buf, XP_U16 buflen, const XP_UCHAR* toPhone,
                       int toPort, XP_Bool forceOld, XP_U16* waitSecsP )
{
    XP_USE( toPort );
    SMSMsgArray* result = NULL;
    pthread_mutex_lock( &state->mutex );

#if defined DEBUG && defined COMMS_CHECKSUM
    Md5SumBuf sb;
    dutil_md5sum( state->dutil, xwe, buf, buflen, &sb );
    XP_LOGFF( "(cmd=%d, gameID=%d): len=%d, sum=%s, toPhone=%s", cmd,
              gameID, buflen, sb.buf, toPhone );
#endif

    ToPhoneRec* rec = getForPhone( state, toPhone, cmd != NONE );

    /* First, add the new message (if present) to the array */
    XP_U32 nowSeconds = dutil_getCurSeconds( state->dutil, xwe );
    if ( cmd != NONE ) {
        addToOutRec( state, rec, cmd, toPort, gameID, buf, buflen, nowSeconds );
    }

    /* rec will be non-null if there's something in it */
    XP_Bool doSend = XP_FALSE;
    if ( rec != NULL ) {
        doSend = forceOld
            || rec->totalSize > SEND_NOW_SIZE
            || MAX_WAIT <= nowSeconds - rec->createSeconds;
        /* other criteria? */
    }

    if ( doSend ) {
        result = toNetMsgs( state, xwe, rec, forceOld );
        freeForPhone( state, toPhone );
    }

    XP_U16 waitSecs = 0;
    if ( !result && !!rec && (rec->nMsgs > 0) ) {
        waitSecs = MAX_WAIT - (nowSeconds - rec->createSeconds);
    }
    *waitSecsP = waitSecs;

    XP_LOGF( "%s() => %p (count=%d, *waitSecs=%d)", __func__, result,
             !!result ? result->nMsgs : 0, *waitSecsP );

    pthread_mutex_unlock( &state->mutex );
    logResult( state, xwe, result, __func__ );
    return result;
} /* smsproto_prepOutbound */

static SMSMsgArray*
appendLocMsg( SMSProto* XP_UNUSED_DBG(state), SMSMsgArray* arr, SMSMsgLoc* msg )
{
    if ( NULL == arr ) {
        arr = XP_CALLOC( state->mpool, sizeof(*arr) );
        arr->format = FORMAT_LOC;
    } else {
        XP_ASSERT( arr->format == FORMAT_LOC );
    }

    arr->u.msgsLoc = XP_REALLOC( state->mpool, arr->u.msgsLoc,
                                 (arr->nMsgs + 1) * sizeof(*arr->u.msgsLoc) );
    arr->u.msgsLoc[arr->nMsgs++] = *msg;
    return arr;
}

static SMSMsgArray*
appendNetMsg( SMSProto* XP_UNUSED_DBG(state), SMSMsgArray* arr, SMSMsgNet* msg )
{
    if ( NULL == arr ) {
        arr = XP_CALLOC( state->mpool, sizeof(*arr) );
        arr->format = FORMAT_NET;
    } else {
        XP_ASSERT( arr->format == FORMAT_NET );
    }

    arr->u.msgsNet = XP_REALLOC( state->mpool, arr->u.msgsNet,
                                 (arr->nMsgs + 1) * sizeof(*arr->u.msgsNet) );
    arr->u.msgsNet[arr->nMsgs++] = *msg;
    return arr;
}

SMSMsgArray*
smsproto_prepInbound( SMSProto* state, XWEnv xwe, const XP_UCHAR* fromPhone,
                      XP_U16 wantPort, const XP_U8* data, XP_U16 len )
{
    XP_LOGFF( "len=%d, fromPhone=%s", len, fromPhone );

#if defined DEBUG && defined COMMS_CHECKSUM
    Md5SumBuf sb;
    dutil_md5sum( state->dutil, xwe, data, len, &sb );
    XP_LOGFF( "(fromPhone=%s, len=%d); sum=%s", fromPhone, len, sb.buf );
#endif

    SMSMsgArray* result = NULL;
    pthread_mutex_lock( &state->mutex );

    XWStreamCtxt* stream = mkStream( state );
    stream_putBytes( stream, data, len );

    XP_U8 proto;
    if ( stream_gotU8( stream, &proto ) ) {
        switch ( proto ) {
        case SMS_PROTO_VERSION_JAVA: {
            XP_U8 msgID, indx, count;
            if ( stream_gotU8( stream, &msgID )
                 && stream_gotU8( stream, &indx )
                 && stream_gotU8( stream, &count )
                 && indx < count ) {
                XP_U16 len = stream_getSize( stream );
                XP_U8 buf[len];
                stream_getBytes( stream, buf, len );
                addMessage( state, fromPhone, msgID, indx, count, buf, len );
                result = completeMsgs( state, result, fromPhone, wantPort, msgID );
                savePartials( state, xwe );
            }
        }
            break;
        case SMS_PROTO_VERSION_COMBO: {
            XP_U8 oneLen, msgID;
            while ( stream_gotU8( stream, &oneLen )
                    && stream_gotU8( stream, &msgID ) ) {
                XP_U8 tmp[oneLen];
                stream_getBytes( stream, tmp, oneLen );

                XWStreamCtxt* msgStream = mkStream( state );
                stream_putBytes( msgStream, tmp, oneLen );
                
                XP_U32 gameID;
                XP_U16 port;
                SMS_CMD cmd;
                if ( headerFromStream( msgStream, &cmd, &port, &gameID ) ) {
                    XP_U16 msgLen = stream_getSize( msgStream );
                    XP_U8 buf[msgLen];
                    if ( stream_gotBytes( msgStream, buf, msgLen ) ) {
                        if ( port == wantPort ) {
                            SMSMsgLoc msg = { .len = msgLen,
                                              .cmd = cmd,
                                              .gameID = gameID,
                                              .data = XP_MALLOC( state->mpool, msgLen ),
                            };
                            XP_MEMCPY( msg.data, buf, msgLen );
                            result = appendLocMsg( state, result, &msg );
                        } else {
                            XP_LOGF( "%s(): expected port %d, got %d", __func__,
                                     wantPort, port );
                        }
                    }
                }
                destroyStream( msgStream );
            }
        }
            break;
        default:
            /* Don't assert! happens all the time */
            XP_LOGF( "%s(): unexpected proto %d", __func__, proto );
            break;
        }
    }

    destroyStream( stream );

    XP_LOGFF( "=> %p (len=%d)", result, (!!result) ? result->nMsgs : 0 );
    logResult( state, xwe, result, __func__ );

    pthread_mutex_unlock( &state->mutex );
    return result;
}

void
smsproto_freeMsgArray( SMSProto* state, SMSMsgArray* arr )
{
    pthread_mutex_lock( &state->mutex );

    for ( int ii = 0; ii < arr->nMsgs; ++ii ) {
        XP_U8** ptr = arr->format == FORMAT_LOC
            ? &arr->u.msgsLoc[ii].data : &arr->u.msgsNet[ii].data;
        XP_FREEP( state->mpool, ptr );
    }

    void** ptr;
    switch( arr->format ) {
    case FORMAT_LOC:
        ptr = (void**)&arr->u.msgsLoc;
        break;
    case FORMAT_NET:
        ptr = (void**)&arr->u.msgsNet;
        break;
    default:
        XP_ASSERT(0);
        ptr = NULL;
    }
    XP_FREEP( state->mpool, ptr );
    XP_FREEP( state->mpool, &arr );
    pthread_mutex_unlock( &state->mutex );
}

#if defined DEBUG && defined COMMS_CHECKSUM
static void
logResult( const SMSProto* state, XWEnv xwe, const SMSMsgArray* result,
           const char* caller )
{
    if ( !!result ) {
        for ( int ii = 0; ii < result->nMsgs; ++ii ) {
            XP_U8* data;
            XP_U16 len = 0;
            switch ( result->format ) {
            case FORMAT_LOC: {
                SMSMsgLoc* msgsLoc = &result->u.msgsLoc[ii];
                data = msgsLoc->data;
                len = msgsLoc->len;
            }
                break;
            case FORMAT_NET: {
                SMSMsgNet* msgsNet = &result->u.msgsNet[ii];
                data = msgsNet->data;
                len = msgsNet->len;
            }
                break;
            default:
                XP_ASSERT(0);
            }
            if ( 0 == len ) {
                XP_LOGFF( "%s() => datum[%d] len: 0", caller, ii );
            } else {
                Md5SumBuf sb;
                dutil_md5sum( state->dutil, xwe, data, len, &sb );
                XP_LOGFF( "%s() => datum[%d] sum: %s, len: %d", caller, ii,
                          sb.buf, len );
            }
        }
    }
}
#endif

static void
freeMsg( SMSProto* XP_UNUSED_DBG(state), MsgRec** msgp )
{
    XP_FREEP( state->mpool, &(*msgp)->msgNet.data );
    XP_FREEP( state->mpool, msgp );
}

static void
freeRec( SMSProto* state, ToPhoneRec* rec )
{
    for ( XP_U16 jj = 0; jj < rec->nMsgs; ++jj ) {
        freeMsg( state, &rec->msgs[jj] );
    }
    XP_FREEP( state->mpool, &rec->msgs );
}

static ToPhoneRec*
getForPhone( SMSProto* state, const XP_UCHAR* phone, XP_Bool create )
{
    ToPhoneRec* rec = NULL;
    for ( XP_U16 ii = 0; !rec && ii < state->nToPhones; ++ii ) {
        if ( 0 == XP_STRCMP( state->toPhoneRecs[ii].phone, phone ) ) {
            rec = &state->toPhoneRecs[ii];
        }
    }

    if ( !rec && create ) {
        state->toPhoneRecs = XP_REALLOC( state->mpool, state->toPhoneRecs,
                                         (1 + state->nToPhones) * sizeof(*state->toPhoneRecs) );
        rec = &state->toPhoneRecs[state->nToPhones++];
        XP_MEMSET( rec, 0, sizeof(*rec) );
        XP_STRCAT( rec->phone, phone );
    }

    return rec;
}

static void
freeForPhone( SMSProto* state, const XP_UCHAR* phone )
{
    for ( XP_U16 ii = 0; ii < state->nToPhones; ++ii ) {
        if ( 0 == XP_STRCMP( state->toPhoneRecs[ii].phone, phone ) ) {
            freeRec( state, &state->toPhoneRecs[ii] );

            XP_U16 nAbove = state->nToPhones - ii - 1;
            XP_ASSERT( nAbove >= 0 );
            if ( nAbove > 0 ) {
                XP_MEMMOVE( &state->toPhoneRecs[ii], &state->toPhoneRecs[ii+1],
                            nAbove * sizeof(*state->toPhoneRecs) );
            }
            --state->nToPhones;
            if ( 0 == state->nToPhones ) {
                XP_FREEP( state->mpool, &state->toPhoneRecs );
            } else {
                state->toPhoneRecs = XP_REALLOC( state->mpool, state->toPhoneRecs,
                                                 state->nToPhones * sizeof(*state->toPhoneRecs) );
            }
            break;
        }
    }
}

static void
addToOutRec( SMSProto* state, ToPhoneRec* rec, SMS_CMD cmd,
             XP_U16 port, XP_U32 gameID, const XP_U8* buf, XP_U16 buflen,
             XP_U32 nowSeconds )
{
    XWStreamCtxt* stream = mkStream( state );
    headerToStream( stream, cmd, port, gameID );
    stream_putBytes( stream, buf, buflen );
    
    MsgRec* mRec = XP_CALLOC( state->mpool, sizeof(*rec) );
    XP_U16 len = stream_getSize( stream );
    mRec->msgNet.len = len;
    mRec->msgNet.data = XP_MALLOC( state->mpool, len );
    XP_MEMCPY( mRec->msgNet.data, stream_getPtr(stream), len );
    destroyStream( stream );

    mRec->createSeconds = nowSeconds;

    rec->msgs = XP_REALLOC( state->mpool, rec->msgs, (1 + rec->nMsgs) * sizeof(*rec->msgs) );
    rec->msgs[rec->nMsgs++] = mRec;
    rec->totalSize += len;
    XP_LOGFF( "added msg to %s of len %d; total now %d", rec->phone, len,
              rec->totalSize );

    if ( rec->nMsgs == 1 ) {
        rec->createSeconds = nowSeconds;
    }
}

static MsgIDRec*
getMsgIDRec( SMSProto* state, const XP_UCHAR* fromPhone, int msgID,
             XP_Bool addMissing, int* fromPhoneIndex, int* msgIDIndex )
{
    MsgIDRec* result = NULL;

    FromPhoneRec* fromPhoneRec = NULL;
    for ( int ii = 0; ii < state->nFromPhones; ++ii ) {
        if ( 0 == XP_STRCMP( state->fromPhoneRecs[ii].phone, fromPhone ) ) {
            fromPhoneRec = &state->fromPhoneRecs[ii];
            *fromPhoneIndex = ii;
            break;
        }
    }

    // create and add if not found
    if ( NULL == fromPhoneRec && addMissing ) {
        state->fromPhoneRecs =
            XP_REALLOC( state->mpool, state->fromPhoneRecs,
                        (state->nFromPhones + 1) * sizeof(*state->fromPhoneRecs) );
        *fromPhoneIndex = state->nFromPhones;
        fromPhoneRec = &state->fromPhoneRecs[state->nFromPhones++];
        XP_MEMSET( fromPhoneRec, 0, sizeof(*fromPhoneRec) );
        XP_STRCAT( fromPhoneRec->phone, fromPhone );
    }

    // Now find msgID record
    if ( NULL != fromPhoneRec ) {
        for ( int ii = 0; ii < fromPhoneRec->nMsgIDs; ++ii ) {
            if ( fromPhoneRec->msgIDRecs[ii].msgID == msgID ) {
                result = &fromPhoneRec->msgIDRecs[ii];
                *msgIDIndex = ii;
                break;
            }
        }

        // create and add if not found
        if ( NULL == result && addMissing ) {
            fromPhoneRec->msgIDRecs = XP_REALLOC( state->mpool, fromPhoneRec->msgIDRecs,
                                                  (fromPhoneRec->nMsgIDs + 1)
                                                  * sizeof(*fromPhoneRec->msgIDRecs) );
            MsgIDRec newRec = { .msgID = msgID };
            *msgIDIndex = fromPhoneRec->nMsgIDs;
            result = &fromPhoneRec->msgIDRecs[fromPhoneRec->nMsgIDs];
            fromPhoneRec->msgIDRecs[fromPhoneRec->nMsgIDs++] = newRec;
        }
    }

    return result;
}

/* Messages that are split gather here until complete
 */
static void
addMessage( SMSProto* state, const XP_UCHAR* fromPhone, int msgID, int indx,
            int count, const XP_U8* data, XP_U16 len )
{
    // XP_LOGFF( "phone=%s, msgID=%d, %d/%d", fromPhone, msgID, indx, count );
    XP_ASSERT( 0 < len );
    MsgIDRec* msgIDRec;
    for ( ; ; ) {
        int fromPhoneIndex;
        int msgIDIndex;
        msgIDRec = getMsgIDRec( state, fromPhone, msgID, XP_TRUE,
                                &fromPhoneIndex, &msgIDIndex );

        /* sanity check... */
        if ( msgIDRec->count == 0 || msgIDRec->count == count ) {
            break;
        }
        freeMsgIDRec( state, msgIDRec, fromPhoneIndex, msgIDIndex );
    }

    /* if it's new, fill in missing fields */
    if ( msgIDRec->count == 0 ) {
        msgIDRec->count = count;    /* in case it's new */
        msgIDRec->parts = XP_CALLOC( state->mpool, count * sizeof(*msgIDRec->parts));
    }

    XP_ASSERT( msgIDRec->parts[indx].len == 0
               || msgIDRec->parts[indx].len == len ); /* replace with same ok */
    msgIDRec->parts[indx].len = len;
    XP_FREEP( state->mpool, &msgIDRec->parts[indx].data ); /* in case non-null (replacement) */
    msgIDRec->parts[indx].data = XP_MALLOC( state->mpool, len );
    XP_MEMCPY( msgIDRec->parts[indx].data, data, len );
}

static void
rmFromPhoneRec( SMSProto* state, int fromPhoneIndex )
{
    FromPhoneRec* fromPhoneRec = &state->fromPhoneRecs[fromPhoneIndex];
    XP_ASSERT( fromPhoneRec->nMsgIDs == 0 );
    XP_FREEP( state->mpool, &fromPhoneRec->msgIDRecs );

    if ( --state->nFromPhones == 0 ) {
        XP_FREEP( state->mpool, &state->fromPhoneRecs );
    } else {
        XP_U16 nAbove = state->nFromPhones - fromPhoneIndex;
        XP_ASSERT( nAbove >= 0 );
        if ( nAbove > 0 ) {
            XP_MEMMOVE( &state->fromPhoneRecs[fromPhoneIndex], &state->fromPhoneRecs[fromPhoneIndex+1],
                        nAbove * sizeof(*state->fromPhoneRecs) );
        }
        state->fromPhoneRecs = XP_REALLOC( state->mpool, state->fromPhoneRecs,
                                           state->nFromPhones * sizeof(*state->fromPhoneRecs));
    }
}

static void
freeMsgIDRec( SMSProto* state, MsgIDRec* XP_UNUSED_DBG(rec), int fromPhoneIndex, int msgIDIndex )
{
    FromPhoneRec* fromPhoneRec = &state->fromPhoneRecs[fromPhoneIndex];
    MsgIDRec* msgIDRec = &fromPhoneRec->msgIDRecs[msgIDIndex];
    XP_ASSERT( msgIDRec == rec );

    for ( int ii = 0; ii < msgIDRec->count; ++ii ) {
        XP_FREEP( state->mpool, &msgIDRec->parts[ii].data );
    }
    XP_FREEP( state->mpool, &msgIDRec->parts );

    if ( --fromPhoneRec->nMsgIDs > 0 ) {
        XP_U16 nAbove = fromPhoneRec->nMsgIDs - msgIDIndex;
        XP_ASSERT( nAbove >= 0 );
        if ( nAbove > 0 ) {
            XP_MEMMOVE( &fromPhoneRec->msgIDRecs[msgIDIndex], &fromPhoneRec->msgIDRecs[msgIDIndex+1],
                        nAbove * sizeof(*fromPhoneRec->msgIDRecs) );
        }
        fromPhoneRec->msgIDRecs = XP_REALLOC( state->mpool, fromPhoneRec->msgIDRecs,
                                              fromPhoneRec->nMsgIDs
                                              * sizeof(*fromPhoneRec->msgIDRecs));
    } else {
        rmFromPhoneRec( state, fromPhoneIndex );
    }
}

static void
savePartials( SMSProto* state, XWEnv xwe )
{
    XWStreamCtxt* stream = mkStream( state );
    stream_putU8( stream, PARTIALS_FORMAT );

    stream_putU8( stream, state->nFromPhones );
    for ( int ii = 0; ii < state->nFromPhones; ++ii ) {
        const FromPhoneRec* rec = &state->fromPhoneRecs[ii];
        stringToStream( stream, rec->phone );
        stream_putU8( stream, rec->nMsgIDs );
        for ( int jj = 0; jj < rec->nMsgIDs; ++jj ) {
            MsgIDRec* mir = &rec->msgIDRecs[jj];
            stream_putU16( stream, mir->msgID );
            stream_putU8( stream, mir->count );

            /* There's an array here. It may be sparse. Save a len of 0 */
            for ( int kk = 0; kk < mir->count; ++kk ) {
                int len = mir->parts[kk].len;
                stream_putU8( stream, len );
                stream_putBytes( stream, mir->parts[kk].data, len );
            }
        }
    }

    XP_U16 newSize = stream_getSize( stream );
    if ( state->lastStoredSize == 2 && newSize == 2 ) {
        XP_LOGFF( "not storing empty again" );
    } else {
        const XP_UCHAR* keys[] = { KEY_PARTIALS, NULL, };
        dutil_storeStream( state->dutil, xwe, keys, stream );
        state->lastStoredSize = newSize;
    }

    destroyStream( stream );

    LOG_RETURN_VOID();
} /* savePartials */

static void
restorePartials( SMSProto* state, XWEnv xwe )
{
    XWStreamCtxt* stream = mkStream( state );

    const XP_UCHAR* keys[] = { KEY_PARTIALS, NULL, };
    dutil_loadStream( state->dutil, xwe, keys, stream );
    if ( stream_getSize( stream ) >= 1
         && PARTIALS_FORMAT == stream_getU8( stream ) ) {
        int nFromPhones = stream_getU8( stream );
        for ( int ii = 0; ii < nFromPhones; ++ii ) {
            XP_UCHAR phone[32];
            (void)stringFromStreamHere( stream, phone, VSIZE(phone) );
            int nMsgIDs = stream_getU8( stream );
            /* XP_LOGF( "%s(): got %d message records for phone %s", __func__, */
            /*          nMsgIDs, phone ); */
            for ( int jj = 0; jj < nMsgIDs; ++jj ) {
                XP_U16 msgID = stream_getU16( stream );
                int count = stream_getU8( stream );
                /* XP_LOGF( "%s(): got %d records for msgID %d", __func__, count, msgID ); */
                for ( int kk = 0; kk < count; ++kk ) {
                    int len = stream_getU8( stream );
                    if ( 0 < len ) {
                        XP_U8 buf[len];
                        stream_getBytes( stream, buf, len );
                        addMessage( state, phone, msgID, kk, count, buf, len );
                    }
                }
            }
        }
    }
    destroyStream( stream );
}

static SMSMsgArray*
completeMsgs( SMSProto* state, SMSMsgArray* arr, const XP_UCHAR* fromPhone,
              XP_U16 wantPort, int msgID )
{
    int fromPhoneIndex, msgIDIndex;
    MsgIDRec* rec = getMsgIDRec( state, fromPhone, msgID, XP_FALSE,
                                 &fromPhoneIndex, &msgIDIndex);
    if ( !rec ) {
        XP_LOGFF( "no rec for phone %s, msgID %d", fromPhone, msgID );
        XP_ASSERT( 0 );
    }

    int len = 0;
    XP_Bool haveAll = XP_TRUE;
    for ( int ii = 0; ii < rec->count; ++ii ) {
        if ( rec->parts[ii].len == 0 ) {
            haveAll = XP_FALSE;
            break;
        } else {
            len += rec->parts[ii].len;
        }
    }

    if ( haveAll ) {
        XWStreamCtxt* stream = mkStream( state );
        for ( int ii = 0; ii < rec->count; ++ii ) {
            stream_putBytes( stream, rec->parts[ii].data, rec->parts[ii].len );
        }

        XP_U32 gameID;
        XP_U16 port;
        SMS_CMD cmd;
        if ( headerFromStream( stream, &cmd, &port, &gameID ) ) {
            XP_U16 len = stream_getSize( stream );
            SMSMsgLoc msg = { .len = len,
                              .cmd = cmd,
                              .gameID = gameID,
                              .data = XP_MALLOC( state->mpool, len ),
            };
            if ( stream_gotBytes( stream, msg.data, len ) && port == wantPort ) {
                arr = appendLocMsg( state, arr, &msg );
            } else {
                XP_LOGFF( "expected port %d, got %d", wantPort, port );
                XP_FREEP( state->mpool, &msg.data );
            }
        }
        destroyStream( stream );

        freeMsgIDRec( state, rec, fromPhoneIndex, msgIDIndex );
    }

    return arr;
}

static SMSMsgArray*
toNetMsgs( SMSProto* state, XWEnv xwe, ToPhoneRec* rec, XP_Bool forceOld )
{
    SMSMsgArray* result = NULL;

    for ( XP_U16 ii = 0; ii < rec->nMsgs; ) {
        // XP_LOGF( "%s(): looking at msg %d of %d", __func__, ii, rec->nMsgs );
        XP_U16 count = (rec->msgs[ii]->msgNet.len + (MAX_LEN_BINARY-1)) / MAX_LEN_BINARY;

        /* First, see if this message and some number of its neighbors can be
           combined */
        int last = ii;
        int sum = 0;
        if ( count == 1 && !forceOld ) {
            for ( ; last < rec->nMsgs; ++last ) {
                int nextLen = rec->msgs[last]->msgNet.len;
                if ( sum + nextLen > MAX_LEN_BINARY ) {
                    break;
                }
                sum += nextLen;
            }
        }

        if ( last > ii ) {
            int nMsgs = last - ii;
            if ( nMsgs > 1 ) {
                XP_LOGFF( "combining %d through %d (%d msgs)", ii, last - 1, nMsgs );
            }
            int len = 1 + sum + (nMsgs * 2); /* 1: len & msgID */
            SMSMsgNet newMsg = { .len = len,
                                 .data = XP_MALLOC( state->mpool, len )
            };
            int indx = 0;
            newMsg.data[indx++] = SMS_PROTO_VERSION_COMBO;
            for ( int jj = ii; jj < last; ++jj ) {
                const SMSMsgNet* msg = &rec->msgs[jj]->msgNet;
                newMsg.data[indx++] = msg->len;
                newMsg.data[indx++] = nextMsgID( state, xwe );
                XP_MEMCPY( &newMsg.data[indx], msg->data, msg->len ); /* bad! */
                indx += msg->len;
            }
            result = appendNetMsg( state, result, &newMsg );
            ii = last;
        } else {
            int msgID = nextMsgID( state, xwe );
            const SMSMsgNet* msg = &rec->msgs[ii]->msgNet;
            XP_U8* nextStart = msg->data;
            XP_U16 lenLeft = msg->len;
            for ( XP_U16 indx = 0; indx < count; ++indx ) {
                XP_ASSERT( lenLeft > 0 );
                XP_U16 useLen = lenLeft;
                if ( useLen >= MAX_LEN_BINARY ) {
                    useLen = MAX_LEN_BINARY;
                }
                lenLeft -= useLen;

                SMSMsgNet newMsg = { .len = useLen + 4,
                                     .data = XP_MALLOC( state->mpool, useLen + 4 )
                };
                newMsg.data[0] = SMS_PROTO_VERSION_JAVA;
                newMsg.data[1] = msgID;
                newMsg.data[2] = indx;
                newMsg.data[3] = count;
                XP_MEMCPY( newMsg.data + 4, nextStart, useLen );
                nextStart += useLen;

                result = appendNetMsg( state, result, &newMsg );
            }
            ++ii;
        }
    }

    return result;
} /* toMsgs */

static int
nextMsgID( SMSProto* state, XWEnv xwe )
{
    int result = ++state->nNextID % 0x000000FF;
    const XP_UCHAR* keys[] = { KEY_NEXTID, NULL, };
    dutil_storePtr( state->dutil, xwe, keys, &state->nNextID,
                    sizeof(state->nNextID) );
    LOG_RETURNF( "%d", result );
    return result;
}

static XWStreamCtxt*
mkStream( SMSProto* state )
{
    XWStreamCtxt* stream = mem_stream_make_raw( MPPARM(state->mpool)
                                                dutil_getVTManager(state->dutil) );
    return stream;
}

static void
destroyStream( XWStreamCtxt* stream )
{
    stream_destroy( stream );
}

#ifdef DEBUG
void
smsproto_runTests( MPFORMAL XWEnv xwe, XW_DUtilCtxt* dutil )
{
    LOG_FUNC();
    SMSProto* state = smsproto_init( mpool, xwe, dutil );

    const int smallSiz = 20;
    const char* phones[] = {"1234", "3456", "5467", "9877"};
    const char* buf = "asoidfaisdfoausdf aiousdfoiu asodfu oiuasdofi oiuaosiduf oaisudf oiasd f"
        ";oiaisdjfljiojaklj asdlkjalskdjf laksjd flkjasdlfkj aldsjkf lsakdjf lkjsad flkjsd fl;kj"
        "asdifaoaosidfoiauosidufoaus doifuoaiusdoifu aoisudfoaisd foia sdoifuasodfu aosiud foiuas odfiu asd"
        "aosdoiaosdoiisidfoiosi isoidufoisu doifuoisud oiuoi98a90iu-asjdfoiasdfij"
        ;
    const XP_Bool forceOld = XP_TRUE;

    SMSMsgArray* arrs[VSIZE(phones)];
    for ( int ii = 0; ii < VSIZE(arrs); ++ii ) {
        arrs[ii] = NULL;
    }

    /* Loop until all the messages are ready. */
    const XP_U32 gameID = 12344321;
    const int port = 1;
    for ( XP_Bool firstTime = XP_TRUE; ; firstTime = XP_FALSE) {
        XP_Bool allDone = XP_TRUE;
        for ( int ii = 0; ii < VSIZE(arrs); ++ii ) {
            XP_U16 waitSecs;
            if ( firstTime ) {
                XP_U16 len = (ii + 1) * 30;
                arrs[ii] = smsproto_prepOutbound( state, xwe, DATA, gameID, buf, len, phones[ii],
                                                  port, forceOld, &waitSecs );
            } else if ( NULL == arrs[ii]) {
                arrs[ii] = smsproto_prepOutbound( state, xwe, DATA, gameID, NULL, 0, phones[ii],
                                                  port, forceOld, &waitSecs );
            } else {
                continue;
            }
            allDone = allDone & (waitSecs == 0 && !!arrs[ii]);
        }
        if ( allDone ) {
            break;
        } else {
            (void)sleep( 2 );
        }
    }

    for ( int indx = 0; ; ++indx ) {
        XP_Bool haveOne = XP_FALSE;
        for ( int ii = 0; ii < VSIZE(arrs); ++ii ) {
            if (!!arrs[ii] && indx < arrs[ii]->nMsgs) {
                XP_ASSERT( arrs[ii]->format == FORMAT_NET );
                haveOne = XP_TRUE;
                SMSMsgArray* outArr = smsproto_prepInbound( state, xwe, phones[ii], port,
                                                            arrs[ii]->u.msgsNet[indx].data,
                                                            arrs[ii]->u.msgsNet[indx].len );
                if ( !!outArr ) {
                    XP_ASSERT( outArr->format == FORMAT_LOC );
                    SMSMsgLoc* msg = &outArr->u.msgsLoc[0];
                    XP_ASSERT( msg->gameID == gameID );
                    XP_ASSERT( msg->cmd == DATA );
                    // XP_LOGF( "%s(): got msgID %d", __func__, msg->msgID );
                    XP_ASSERT( outArr->nMsgs == 1 );
                    XP_ASSERT( 0 == memcmp(buf, msg->data, (ii + 1) * 30) );
                    smsproto_freeMsgArray( state, outArr );

                    smsproto_freeMsgArray( state, arrs[ii] );
                    arrs[ii] = NULL;
                }
            }
        }
        if (!haveOne) {
            break;
        }
    }

    /* Now let's send a bunch of small messages that should get combined */
    for ( int nUsed = 0; ; ++nUsed ) {
        XP_U16 waitSecs;
        SMSMsgArray* sendArr = smsproto_prepOutbound( state, xwe, DATA, gameID, &buf[nUsed],
                                                      smallSiz, phones[0], port,
                                                      XP_FALSE, &waitSecs );
        if ( sendArr == NULL ) {
            XP_LOGF( "%s(): msg[%d] of len %d sent; still not ready", __func__, nUsed, smallSiz );
            continue;
        }

        XP_ASSERT( waitSecs == 0 );
        XP_ASSERT( sendArr->format == FORMAT_NET );
        int totalBack = 0;
        for ( int jj = 0; jj < sendArr->nMsgs; ++jj ) {
            SMSMsgArray* recvArr = smsproto_prepInbound( state, xwe, phones[0], port,
                                                         sendArr->u.msgsNet[jj].data,
                                                         sendArr->u.msgsNet[jj].len );

            if ( !!recvArr ) {
                XP_ASSERT( recvArr->format == FORMAT_LOC );
                XP_LOGF( "%s(): got %d msgs (from %d)", __func__, recvArr->nMsgs, nUsed + 1 );
                for ( int kk = 0; kk < recvArr->nMsgs; ++kk ) {
                    SMSMsgLoc* msg = &recvArr->u.msgsLoc[kk];
                    // XP_LOGF( "%s(): got msgID %d", __func__, msg->msgID );
                    XP_ASSERT( msg->gameID == gameID );
                    XP_ASSERT( msg->cmd == DATA );
                    XP_ASSERT( msg->len == smallSiz );
                    XP_ASSERT( 0 == memcmp( msg->data, &buf[totalBack], smallSiz ) );
                    ++totalBack;
                }

                smsproto_freeMsgArray( state, recvArr );
            }
        }
        XP_ASSERT( forceOld || totalBack == nUsed + 1 );
        XP_LOGF( "%s(): %d messages checked out", __func__, totalBack );
        smsproto_freeMsgArray( state, sendArr );
        break;
    }

    /* Now let's add a too-long message and unpack only the first part. Make
       sure it's cleaned up correctly */
    XP_U16 waitSecs;
    SMSMsgArray* arr = smsproto_prepOutbound( state, xwe, DATA, gameID, buf, 200, "33333",
                                              port, XP_TRUE, &waitSecs );
    XP_ASSERT( !!arr && arr->nMsgs > 1 );
    /* add only part 1 */
    SMSMsgArray* out = smsproto_prepInbound( state, xwe, "33333", port, arr->u.msgsNet[0].data,
                                             arr->u.msgsNet[0].len );
    XP_ASSERT( !out );
    smsproto_freeMsgArray( state, arr );

    /* Try the no-buffer messages */
    XP_LOGF( "%s(): trying DEATH", __func__ );
    arr = smsproto_prepOutbound( state, xwe, DEATH, gameID, NULL, 0, "33333",
                                 port, XP_TRUE, &waitSecs );
    XP_ASSERT( arr->format == FORMAT_NET );
    out = smsproto_prepInbound( state, xwe, "33333", port,
                                arr->u.msgsNet[0].data,
                                arr->u.msgsNet[0].len );
    XP_ASSERT( out->format == FORMAT_LOC );
    XP_ASSERT( out->u.msgsLoc[0].cmd == DEATH );
    XP_ASSERT( out->u.msgsLoc[0].gameID == gameID );
    smsproto_freeMsgArray( state, arr );
    smsproto_freeMsgArray( state, out );
    XP_LOGF( "%s(): DEATH checked out", __func__ );

    /* Test port mismatch */
    arr = smsproto_prepOutbound( state, xwe, DEATH, gameID, NULL, 0, "33333",
                                 port, XP_TRUE, &waitSecs );
    XP_ASSERT( arr->format == FORMAT_NET );
    out = smsproto_prepInbound( state, xwe, "33333", port + 1,
                                arr->u.msgsNet[0].data,
                                arr->u.msgsNet[0].len );
    XP_ASSERT( out == NULL );
    smsproto_freeMsgArray( state, arr );
    XP_LOGF( "%s(): mismatched port test done", __func__ );

    /* now a message that's unpacked across multiple sessions to test store/load */
    XP_LOGF( "%s(): testing store/restore", __func__ );
    arr = smsproto_prepOutbound( state, xwe, DATA, gameID, (XP_U8*)buf, 200, "33333",
                                 port, XP_TRUE, &waitSecs );
    for ( int ii = 0; ii < arr->nMsgs; ++ii ) {
        SMSMsgArray* out = smsproto_prepInbound( state, xwe, "33333", port,
                                                 arr->u.msgsNet[ii].data,
                                                 arr->u.msgsNet[ii].len );
        if ( !!out ) {
            XP_ASSERT( out->nMsgs == 1);
            XP_ASSERT( out->format == FORMAT_LOC );
            XP_LOGF( "%s(): got the message on the %dth loop", __func__, ii );
            XP_ASSERT( out->u.msgsLoc[0].len == 200 );
            XP_ASSERT( 0 == memcmp( out->u.msgsLoc[0].data, buf, 200 ) );
            smsproto_freeMsgArray( state, out );
            break;
        }
        smsproto_free( state ); /* give it a chance to store state */
        state = smsproto_init( mpool, xwe, dutil );
    }

    /* Really bad to pass a different state than was created with, but now
       since only mpool is used and it's the same for all states, let it
       go. */
    smsproto_freeMsgArray( state, arr ); /* give it a chance to store state */

    smsproto_free( state );
    LOG_RETURN_VOID();
}
#endif

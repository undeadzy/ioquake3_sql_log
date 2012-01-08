/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// sv_game.c -- interface to the game dll

#include "server.h"

#include "../botlib/botlib.h"

botlib_export_t	*botlib_export;

// these functions must be used instead of pointer arithmetic, because
// the game allocates gentities with private information after the server shared part
int	SV_NumForGentity( sharedEntity_t *ent ) {
	int		num;

	num = ( (byte *)ent - (byte *)sv.gentities ) / sv.gentitySize;

	return num;
}

sharedEntity_t *SV_GentityNum( int num ) {
	sharedEntity_t *ent;

	ent = (sharedEntity_t *)((byte *)sv.gentities + sv.gentitySize*(num));

	return ent;
}

playerState_t *SV_GameClientNum( int num ) {
	playerState_t	*ps;

	ps = (playerState_t *)((byte *)sv.gameClients + sv.gameClientSize*(num));

	return ps;
}

svEntity_t	*SV_SvEntityForGentity( sharedEntity_t *gEnt ) {
	if ( !gEnt || gEnt->s.number < 0 || gEnt->s.number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}
	return &sv.svEntities[ gEnt->s.number ];
}

sharedEntity_t *SV_GEntityForSvEntity( svEntity_t *svEnt ) {
	int		num;

	num = svEnt - sv.svEntities;
	return SV_GentityNum( num );
}

/*
===============
SV_GameSendServerCommand

Sends a command string to a client
===============
*/
void SV_GameSendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", text );
	} else {
		if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
			return;
		}
		SV_SendServerCommand( svs.clients + clientNum, "%s", text );	
	}
}


/*
===============
SV_GameDropClient

Disconnects the client with a message
===============
*/
void SV_GameDropClient( int clientNum, const char *reason ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		return;
	}
	SV_DropClient( svs.clients + clientNum, reason );	
}


/*
=================
SV_SetBrushModel

sets mins and maxs for inline bmodels
=================
*/
void SV_SetBrushModel( sharedEntity_t *ent, const char *name ) {
	clipHandle_t	h;
	vec3_t			mins, maxs;

	if (!name) {
		Com_Error( ERR_DROP, "SV_SetBrushModel: NULL" );
	}

	if (name[0] != '*') {
		Com_Error( ERR_DROP, "SV_SetBrushModel: %s isn't a brush model", name );
	}


	ent->s.modelindex = atoi( name + 1 );

	h = CM_InlineModel( ent->s.modelindex );
	CM_ModelBounds( h, mins, maxs );
	VectorCopy (mins, ent->r.mins);
	VectorCopy (maxs, ent->r.maxs);
	ent->r.bmodel = qtrue;

	ent->r.contents = -1;		// we don't know exactly what is in the brushes

	SV_LinkEntity( ent );		// FIXME: remove
}



/*
=================
SV_inPVS

Also checks portalareas so that doors block sight
=================
*/
qboolean SV_inPVS (const vec3_t p1, const vec3_t p2)
{
	int		leafnum;
	int		cluster;
	int		area1, area2;
	byte	*mask;

	leafnum = CM_PointLeafnum (p1);
	cluster = CM_LeafCluster (leafnum);
	area1 = CM_LeafArea (leafnum);
	mask = CM_ClusterPVS (cluster);

	leafnum = CM_PointLeafnum (p2);
	cluster = CM_LeafCluster (leafnum);
	area2 = CM_LeafArea (leafnum);
	if ( mask && (!(mask[cluster>>3] & (1<<(cluster&7)) ) ) )
		return qfalse;
	if (!CM_AreasConnected (area1, area2))
		return qfalse;		// a door blocks sight
	return qtrue;
}


/*
=================
SV_inPVSIgnorePortals

Does NOT check portalareas
=================
*/
qboolean SV_inPVSIgnorePortals( const vec3_t p1, const vec3_t p2)
{
	int		leafnum;
	int		cluster;
	byte	*mask;

	leafnum = CM_PointLeafnum (p1);
	cluster = CM_LeafCluster (leafnum);
	mask = CM_ClusterPVS (cluster);

	leafnum = CM_PointLeafnum (p2);
	cluster = CM_LeafCluster (leafnum);

	if ( mask && (!(mask[cluster>>3] & (1<<(cluster&7)) ) ) )
		return qfalse;

	return qtrue;
}


/*
========================
SV_AdjustAreaPortalState
========================
*/
void SV_AdjustAreaPortalState( sharedEntity_t *ent, qboolean open ) {
	svEntity_t	*svEnt;

	svEnt = SV_SvEntityForGentity( ent );
	if ( svEnt->areanum2 == -1 ) {
		return;
	}
	CM_AdjustAreaPortalState( svEnt->areanum, svEnt->areanum2, open );
}


/*
==================
SV_EntityContact
==================
*/
qboolean	SV_EntityContact( vec3_t mins, vec3_t maxs, const sharedEntity_t *gEnt, int capsule ) {
	const float	*origin, *angles;
	clipHandle_t	ch;
	trace_t			trace;

	// check for exact collision
	origin = gEnt->r.currentOrigin;
	angles = gEnt->r.currentAngles;

	ch = SV_ClipHandleForEntity( gEnt );
	CM_TransformedBoxTrace ( &trace, vec3_origin, vec3_origin, mins, maxs,
		ch, -1, origin, angles, capsule );

	return trace.startsolid;
}


/*
===============
SV_GetServerinfo

===============
*/
void SV_GetServerinfo( char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetServerinfo: bufferSize == %i", bufferSize );
	}
	Q_strncpyz( buffer, Cvar_InfoString( CVAR_SERVERINFO ), bufferSize );
}

/*
===============
SV_LocateGameData

===============
*/
void SV_LocateGameData( sharedEntity_t *gEnts, int numGEntities, int sizeofGEntity_t,
					   playerState_t *clients, int sizeofGameClient ) {
	sv.gentities = gEnts;
	sv.gentitySize = sizeofGEntity_t;
	sv.num_entities = numGEntities;

	sv.gameClients = clients;
	sv.gameClientSize = sizeofGameClient;
}


/*
===============
SV_GetUsercmd

===============
*/
void SV_GetUsercmd( int clientNum, usercmd_t *cmd ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		Com_Error( ERR_DROP, "SV_GetUsercmd: bad clientNum:%i", clientNum );
	}
	*cmd = svs.clients[clientNum].lastUsercmd;
}

//==============================================

static int	FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}

/*
====================
SV_GameSystemCalls

The module is making a system call
====================
*/
intptr_t SV_GameSystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case G_PRINT:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "qagame_QVM", "server", "G_PRINT", (const char *)VMA(1));
#endif
		Com_Printf( "%s", (const char*)VMA(1) );
		return 0;
	case G_ERROR:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "qagame_QVM", "server", "G_ERROR", (const char *)VMA(1));
#endif
		Com_Error( ERR_DROP, "%s", (const char*)VMA(1) );
		return 0;
	case G_MILLISECONDS:
	{
		int res = Sys_Milliseconds();
#ifdef USE_SQLITE3
		sql_insert_int(sql, "qagame_QVM", "server", "G_MILLISECONDS", res);
#endif
		return res;
	}
	case G_CVAR_REGISTER:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_CVAR_REGISTER", "%s %s %d", (const char *)VMA(2), (const char *)VMA(3), args[4]);
#endif
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4] ); 
		return 0;
	case G_CVAR_UPDATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame_QVM", "server", "G_CVAR_UPDATE");
//#endif
		Cvar_Update( VMA(1) );
		return 0;
	case G_CVAR_SET:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_CVAR_SET", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		Cvar_SetSafe( (const char *)VMA(1), (const char *)VMA(2) );
		return 0;
	case G_CVAR_VARIABLE_INTEGER_VALUE:
	{
		int res = Cvar_VariableIntegerValue( (const char *)VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_VAR_VARIABLE_INTEGER_VALUE", "%s %d", (const char *)VMA(1), res);
#endif
		return res;
	}
	case G_CVAR_VARIABLE_STRING_BUFFER:
		Cvar_VariableStringBuffer( VMA(1), VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_VAR_VARIABLE_STRING_VALUE", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		return 0;
	case G_ARGC:
	{
		int res = Cmd_Argc();
#ifdef USE_SQLITE3
		sql_insert_int(sql, "qagame_QVM", "server", "G_ARGC", res);
#endif
		return res;
	}
	case G_ARGV:
	{
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_ARGV", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;
	}
	case G_SEND_CONSOLE_COMMAND:
#ifdef USE_SQLITE3
		{
			const char *when;
			switch (args[1]) {
			case EXEC_NOW:
				when = "NOW"; break;
			case EXEC_INSERT:
				when = "INSERT"; break;
			case EXEC_APPEND:
				when = "APPEND"; break;
			default:
				when = "BAD_EXEC"; break;
			}
			sql_insert_var_text(sql, "qagame_QVM", "server", "G_SEND_CONSOLE_COMMAND", "%s %s", when, (const char *)VMA(2));
		}
#endif
		Cbuf_ExecuteText( args[1], VMA(2) );
		return 0;

	case G_FS_FOPEN_FILE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "qagame_QVM", "server", "G_FS_FOPEN_FILE", (const char *)VMA(1));
//#endif
		return FS_FOpenFileByMode( VMA(1), VMA(2), args[3] );
	case G_FS_READ:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "qagame_QVM", "server", "G_FS_READ", args[2]);
//#endif
		FS_Read2( VMA(1), args[2], args[3] );
		return 0;
	case G_FS_WRITE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "qagame_QVM", "server", "G_FS_WRITE", args[2]);
//#endif
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case G_FS_FCLOSE_FILE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "qagame_QVM", "server", "G_FS_FCLOSE_FILE", args[1]);
//#endif
		FS_FCloseFile( args[1] );
		return 0;
	case G_FS_GETFILELIST:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "qagame_QVM", "server", "G_FS_GETFILELIST", "%s %s", VMA(1), VMA(2));
//#endif
		return FS_GetFileList( VMA(1), VMA(2), VMA(3), args[4] );
	case G_FS_SEEK:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "qagame_QVM", "server", "G_FS_SEEK", "%d %d %d", args[1], args[2], args[3]);
//#endif
		return FS_Seek( args[1], args[2], args[3] );

	case G_LOCATE_GAME_DATA:
#ifdef USE_SQLITE3
		// XXX Update with the rest:
		// void SV_LocateGameData( sharedEntity_t *gEnts, int numGEntities, int sizeofGEntity_t,
		//                         playerState_t *clients, int sizeofGameClient ) {
		sql_insert_null(sql, "qagame_QVM", "server", "G_LOCATE_GAME_DATA");
#endif
		SV_LocateGameData( VMA(1), args[2], args[3], VMA(4), args[5] );
		return 0;
	case G_DROP_CLIENT:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_DROP_CLIENT", "%d %s", args[1], (const char *)VMA(2));
#endif
		SV_GameDropClient( args[1], VMA(2) );
		return 0;
	case G_SEND_SERVER_COMMAND:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_SEND_SERVER_COMMAND", "%d %s", args[1], (const char *)VMA(2));
#endif
		SV_GameSendServerCommand( args[1], VMA(2) );
		return 0;
	case G_LINKENTITY:
		SV_LinkEntity( VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "qagame_QVM", "server", "G_LINKENTITY", (sharedEntity_t *)VMA(1), sizeof(sharedEntity_t));
#endif
		return 0;
	case G_UNLINKENTITY:
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "qagame_QVM", "server", "G_LINKENTITY", (sharedEntity_t *)VMA(1), sizeof(sharedEntity_t));
#endif
		SV_UnlinkEntity( VMA(1) );
		return 0;
	case G_ENTITIES_IN_BOX:
	{
		int res = SV_AreaEntities( VMA(1), VMA(2), VMA(3), args[4] );
#ifdef USE_SQLITE3
		sql_insert_null(sql, "qagame_QVM", "server", "G_ENTITIES_IN_BOX");
#endif
		return res;
	}
	case G_ENTITY_CONTACT:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "qagame_QVM", "server", "G_ENTITY_CONTACT");
#endif
		return SV_EntityContact( VMA(1), VMA(2), VMA(3), /*int capsule*/ qfalse );
	case G_ENTITY_CONTACTCAPSULE:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "qagame_QVM", "server", "G_ENTITY_CONTACTCAPSULE");
#endif
		return SV_EntityContact( VMA(1), VMA(2), VMA(3), /*int capsule*/ qtrue );
	case G_TRACE:
		SV_Trace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qfalse );
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "qagame_QVM", "server", "G_TRACE", (trace_t *)VMA(1), sizeof(trace_t));
#endif
		return 0;
	case G_TRACECAPSULE:
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "qagame_QVM", "server", "G_TRACE", (trace_t *)VMA(1), sizeof(trace_t));
#endif
		SV_Trace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qtrue );
		return 0;
	case G_POINT_CONTENTS:
	{
		int res = SV_PointContents( VMA(1), args[2] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_POINT_CONTENTS", "%d %d", args[2], res);
#endif
		return res;
	}
	case G_SET_BRUSH_MODEL:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "qagame_QVM", "server", "G_SET_BRUSH_MODEL", (const char *)VMA(2));
#endif
		SV_SetBrushModel( VMA(1), VMA(2) );
		return 0;
	case G_IN_PVS:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "qagame_QVM", "server", "G_IN_PVS");
#endif
		return SV_inPVS( VMA(1), VMA(2) );
	case G_IN_PVS_IGNORE_PORTALS:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "qagame_QVM", "server", "G_IN_PVS_IGNORE_PORTALS");
#endif
		return SV_inPVSIgnorePortals( VMA(1), VMA(2) );

	case G_SET_CONFIGSTRING:
#ifdef USE_SQLITE3
		if (VMA(2) == NULL) {
			sql_insert_int(sql, "qagame_QVM", "server", "G_SET_CONFIGSTRING", args[1]);
		} else {
			sql_insert_var_text(sql, "qagame_QVM", "server", "G_SET_CONFIGSTRING", "%d %s", args[1], (const char *)VMA(2));
		}
#endif
		SV_SetConfigstring( args[1], VMA(2) );
		return 0;
	case G_GET_CONFIGSTRING:
		SV_GetConfigstring( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_GET_CONFIGSTRING", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;
	case G_SET_USERINFO:
#ifdef USE_SQLITE3
		if (VMA(2) == NULL) {
			sql_insert_text(sql, "qagame_QVM", "server", "G_SET_USERINFO", Info_ValueForKey((const char *)VMA(2), "name"));
		} else {
			sql_insert_var_text(sql, "qagame_QVM", "server", "G_SET_USERINFO", "%s %s", Info_ValueForKey((const char *)VMA(2), "name"), (const char *)VMA(2));
		}
#endif
		SV_SetUserinfo( args[1], VMA(2) );
		return 0;
	case G_GET_USERINFO:
		SV_GetUserinfo( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_text(sql, "qagame_QVM", "server", "G_GET_USERINFO", (const char *)VMA(2));
#endif
		return 0;
	case G_GET_SERVERINFO:
		SV_GetServerinfo( VMA(1), args[2] );
#ifdef USE_SQLITE3
		sql_insert_text(sql, "qagame_QVM", "server", "G_GET_SERVERINFO", (const char *)VMA(1));
#endif
		return 0;
	case G_ADJUST_AREA_PORTAL_STATE:
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "qagame_QVM", "server", "G_ADJUST_AREA_PORTAL_STATE", (sharedEntity_t *)VMA(1), sizeof(sharedEntity_t));
#endif
		SV_AdjustAreaPortalState( VMA(1), args[2] );
		return 0;
	case G_AREAS_CONNECTED:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "qagame_QVM", "server", "G_AREAS_CONNECTED", "%d %d", args[1], args[2]);
#endif
		return CM_AreasConnected( args[1], args[2] );

	case G_BOT_ALLOCATE_CLIENT:
	{
		int res = SV_BotAllocateClient();
#ifdef USE_SQLITE3
		sql_insert_int(sql, "qagame_QVM", "server", "G_BOT_ALLOCATE_CLIENT", res);
#endif
		return res;
	}
	case G_BOT_FREE_CLIENT:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "qagame_QVM", "server", "G_BOT_FREE_CLIENT", args[1]);
#endif
		SV_BotFreeClient( args[1] );
		return 0;

	case G_GET_USERCMD:
		SV_GetUsercmd( args[1], VMA(2) );
#ifdef USE_SQLITE3
		// XXX Handle this better than having two entries.  We need to know which client it is and retain the blob.
		sql_insert_int(sql, "qagame_QVM", "server", "G_GET_USERCMD", args[1]);
		sql_insert_blob(sql, "qagame_QVM", "server", "G_GET_USERCMD", (usercmd_t *)VMA(2), sizeof(usercmd_t));
#endif
		return 0;
	case G_GET_ENTITY_TOKEN:
		{
			const char	*s;

			s = COM_Parse( &sv.entityParsePoint );
			Q_strncpyz( VMA(1), s, args[2] );
			if ( !sv.entityParsePoint && !s[0] ) {
#ifdef USE_SQLITE3
				sql_insert_var_text(sql, "qagame_QVM", "server", "G_GET_ENTITY_TOKEN", "%s %d", (const char*)VMA(1), 1);
#endif
				return qfalse;
			} else {
#ifdef USE_SQLITE3
				sql_insert_var_text(sql, "qagame_QVM", "server", "G_GET_ENTITY_TOKEN", "%s %d", (const char*)VMA(1), 0);
#endif
				return qtrue;
			}
		}

	case G_DEBUG_POLYGON_CREATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "G_DEBUG_POLYGON_CREATE");
//#endif
		return BotImport_DebugPolygonCreate( args[1], args[2], VMA(3) );
	case G_DEBUG_POLYGON_DELETE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "G_DEBUG_POLYGON_DELETE");
//#endif
		BotImport_DebugPolygonDelete( args[1] );
		return 0;
	case G_REAL_TIME:
	{
		int res = Com_RealTime( VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_int(sql, "qagame_QVM", "server", "G_REAL_TIME", res);
#endif
		return res;
	}
	case G_SNAPVECTOR:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame_QVM", "server", "G_SNAPVECTOR");
//#endif
		Q_SnapVector(VMA(1));
		return 0;

		//====================================

// XXX Skipping botlib functions for now

	case BOTLIB_SETUP:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_SETUP");
//#endif
		return SV_BotLibSetup();
	case BOTLIB_SHUTDOWN:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_SHUTDOWN");
//#endif
		return SV_BotLibShutdown();
	case BOTLIB_LIBVAR_SET:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_LIBVAR_SET");
//#endif
		return botlib_export->BotLibVarSet( VMA(1), VMA(2) );
	case BOTLIB_LIBVAR_GET:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_LIBVAR_GET");
//#endif
		return botlib_export->BotLibVarGet( VMA(1), VMA(2), args[3] );

	case BOTLIB_PC_ADD_GLOBAL_DEFINE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_PC_ADD_GLOBAL_DEFINE");
//#endif
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case BOTLIB_PC_LOAD_SOURCE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_PC_LOAD_SOURCE");
//#endif
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case BOTLIB_PC_FREE_SOURCE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_PC_FREE_SOURCE");
//#endif
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case BOTLIB_PC_READ_TOKEN:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_PC_READ_TOKEN");
//#endif
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case BOTLIB_PC_SOURCE_FILE_AND_LINE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_PC_SOURCE_FILE_AND_LINE");
//#endif
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case BOTLIB_START_FRAME:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_START_FRAME");
//#endif
		return botlib_export->BotLibStartFrame( VMF(1) );
	case BOTLIB_LOAD_MAP:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_LOAD_MAP");
//#endif
		return botlib_export->BotLibLoadMap( VMA(1) );
	case BOTLIB_UPDATENTITY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_UPDATENTITY");
//#endif
		return botlib_export->BotLibUpdateEntity( args[1], VMA(2) );
	case BOTLIB_TEST:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_TEST");
//#endif
		return botlib_export->Test( args[1], VMA(2), VMA(3), VMA(4) );

	case BOTLIB_GET_SNAPSHOT_ENTITY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_GET_SNAPSHOT_ENTITY");
//#endif
		return SV_BotGetSnapshotEntity( args[1], args[2] );
	case BOTLIB_GET_CONSOLE_MESSAGE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_GET_CONSOLE_MESSAGE");
//#endif
		return SV_BotGetConsoleMessage( args[1], VMA(2), args[3] );
	case BOTLIB_USER_COMMAND:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_USER_COMMAND");
//#endif
		SV_ClientThink( &svs.clients[args[1]], VMA(2) );
		return 0;

	case BOTLIB_AAS_BBOX_AREAS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_BBOX_AREAS");
//#endif
		return botlib_export->aas.AAS_BBoxAreas( VMA(1), VMA(2), VMA(3), args[4] );
	case BOTLIB_AAS_AREA_INFO:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_AREA_INFO");
//#endif
		return botlib_export->aas.AAS_AreaInfo( args[1], VMA(2) );
	case BOTLIB_AAS_ALTERNATIVE_ROUTE_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_ALTERNATIVE_ROUTE_GOAL");
//#endif
		return botlib_export->aas.AAS_AlternativeRouteGoals( VMA(1), args[2], VMA(3), args[4], args[5], VMA(6), args[7], args[8] );
	case BOTLIB_AAS_ENTITY_INFO:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_ENTITY_INFO");
//#endif
		botlib_export->aas.AAS_EntityInfo( args[1], VMA(2) );
		return 0;

	case BOTLIB_AAS_INITIALIZED:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_INITIALIZED");
//#endif
		return botlib_export->aas.AAS_Initialized();
	case BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX");
//#endif
		botlib_export->aas.AAS_PresenceTypeBoundingBox( args[1], VMA(2), VMA(3) );
		return 0;
	case BOTLIB_AAS_TIME:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_TIME");
//#endif
		return FloatAsInt( botlib_export->aas.AAS_Time() );

	case BOTLIB_AAS_POINT_AREA_NUM:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_POINT_AREA_NUM");
//#endif
		return botlib_export->aas.AAS_PointAreaNum( VMA(1) );
	case BOTLIB_AAS_POINT_REACHABILITY_AREA_INDEX:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_POINT_REACHABILITY_AREA_INDEX");
//#endif
		return botlib_export->aas.AAS_PointReachabilityAreaIndex( VMA(1) );
	case BOTLIB_AAS_TRACE_AREAS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_TRACE_AREAS");
//#endif
		return botlib_export->aas.AAS_TraceAreas( VMA(1), VMA(2), VMA(3), VMA(4), args[5] );

	case BOTLIB_AAS_POINT_CONTENTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_POINT_CONTENTS");
//#endif
		return botlib_export->aas.AAS_PointContents( VMA(1) );
	case BOTLIB_AAS_NEXT_BSP_ENTITY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_NEXT_BSP_ENTITY");
//#endif
		return botlib_export->aas.AAS_NextBSPEntity( args[1] );
	case BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY");
//#endif
		return botlib_export->aas.AAS_ValueForBSPEpairKey( args[1], VMA(2), VMA(3), args[4] );
	case BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY");
//#endif
		return botlib_export->aas.AAS_VectorForBSPEpairKey( args[1], VMA(2), VMA(3) );
	case BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY");
//#endif
		return botlib_export->aas.AAS_FloatForBSPEpairKey( args[1], VMA(2), VMA(3) );
	case BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY");
//#endif
		return botlib_export->aas.AAS_IntForBSPEpairKey( args[1], VMA(2), VMA(3) );

	case BOTLIB_AAS_AREA_REACHABILITY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_AREA_REACHABILITY");
//#endif
		return botlib_export->aas.AAS_AreaReachability( args[1] );

	case BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA");
//#endif
		return botlib_export->aas.AAS_AreaTravelTimeToGoalArea( args[1], VMA(2), args[3], args[4] );
	case BOTLIB_AAS_ENABLE_ROUTING_AREA:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_ENABLE_ROUTING_AREA");
//#endif
		return botlib_export->aas.AAS_EnableRoutingArea( args[1], args[2] );
	case BOTLIB_AAS_PREDICT_ROUTE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_PREDICT_ROUTE");
//#endif
		return botlib_export->aas.AAS_PredictRoute( VMA(1), args[2], VMA(3), args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11] );

	case BOTLIB_AAS_SWIMMING:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_SWIMMING");
//#endif
		return botlib_export->aas.AAS_Swimming( VMA(1) );
	case BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT");
//#endif
		return botlib_export->aas.AAS_PredictClientMovement( VMA(1), args[2], VMA(3), args[4], args[5],
			VMA(6), VMA(7), args[8], args[9], VMF(10), args[11], args[12], args[13] );

	case BOTLIB_EA_SAY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_SAY");
//#endif
		botlib_export->ea.EA_Say( args[1], VMA(2) );
		return 0;
	case BOTLIB_EA_SAY_TEAM:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_SAY_TEAM");
//#endif
		botlib_export->ea.EA_SayTeam( args[1], VMA(2) );
		return 0;
	case BOTLIB_EA_COMMAND:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_COMMAND");
//#endif
		botlib_export->ea.EA_Command( args[1], VMA(2) );
		return 0;

	case BOTLIB_EA_ACTION:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_ACTION");
//#endif
		botlib_export->ea.EA_Action( args[1], args[2] );
		return 0;
	case BOTLIB_EA_GESTURE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_GESTURE");
//#endif
		botlib_export->ea.EA_Gesture( args[1] );
		return 0;
	case BOTLIB_EA_TALK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_TALK");
//#endif
		botlib_export->ea.EA_Talk( args[1] );
		return 0;
	case BOTLIB_EA_ATTACK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_ATTACK");
//#endif
		botlib_export->ea.EA_Attack( args[1] );
		return 0;
	case BOTLIB_EA_USE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_USE");
//#endif
		botlib_export->ea.EA_Use( args[1] );
		return 0;
	case BOTLIB_EA_RESPAWN:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_RESPAWN");
//#endif
		botlib_export->ea.EA_Respawn( args[1] );
		return 0;
	case BOTLIB_EA_CROUCH:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_CROUCH");
//#endif
		botlib_export->ea.EA_Crouch( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_UP:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE_UP");
//#endif
		botlib_export->ea.EA_MoveUp( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_DOWN:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE_DOWN");
//#endif
		botlib_export->ea.EA_MoveDown( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_FORWARD:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE_FORWARD");
//#endif
		botlib_export->ea.EA_MoveForward( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_BACK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE_BACK");
//#endif
		botlib_export->ea.EA_MoveBack( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_LEFT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE_LEFT");
//#endif
		botlib_export->ea.EA_MoveLeft( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_RIGHT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE_RIGHT");
//#endif
		botlib_export->ea.EA_MoveRight( args[1] );
		return 0;

	case BOTLIB_EA_SELECT_WEAPON:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_SELECT_WEAPON");
//#endif
		botlib_export->ea.EA_SelectWeapon( args[1], args[2] );
		return 0;
	case BOTLIB_EA_JUMP:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_JUMP");
//#endif
		botlib_export->ea.EA_Jump( args[1] );
		return 0;
	case BOTLIB_EA_DELAYED_JUMP:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_DELAYED_JUMP");
//#endif
		botlib_export->ea.EA_DelayedJump( args[1] );
		return 0;
	case BOTLIB_EA_MOVE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_MOVE");
//#endif
		botlib_export->ea.EA_Move( args[1], VMA(2), VMF(3) );
		return 0;
	case BOTLIB_EA_VIEW:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_VIEW");
//#endif
		botlib_export->ea.EA_View( args[1], VMA(2) );
		return 0;

	case BOTLIB_EA_END_REGULAR:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_END_REGULAR");
//#endif
		botlib_export->ea.EA_EndRegular( args[1], VMF(2) );
		return 0;
	case BOTLIB_EA_GET_INPUT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_GET_INPUT");
//#endif
		botlib_export->ea.EA_GetInput( args[1], VMF(2), VMA(3) );
		return 0;
	case BOTLIB_EA_RESET_INPUT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_EA_RESET_INPUT");
//#endif
		botlib_export->ea.EA_ResetInput( args[1] );
		return 0;

	case BOTLIB_AI_LOAD_CHARACTER:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_LOAD_CHARACTER");
//#endif
		return botlib_export->ai.BotLoadCharacter( VMA(1), VMF(2) );
	case BOTLIB_AI_FREE_CHARACTER:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FREE_CHARACTER");
//#endif
		botlib_export->ai.BotFreeCharacter( args[1] );
		return 0;
	case BOTLIB_AI_CHARACTERISTIC_FLOAT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHARACTERISTIC_FLOAT");
//#endif
		return FloatAsInt( botlib_export->ai.Characteristic_Float( args[1], args[2] ) );
	case BOTLIB_AI_CHARACTERISTIC_BFLOAT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHARACTERISTIC_BFLOAT");
//#endif
		return FloatAsInt( botlib_export->ai.Characteristic_BFloat( args[1], args[2], VMF(3), VMF(4) ) );
	case BOTLIB_AI_CHARACTERISTIC_INTEGER:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHARACTERISTIC_INTEGER");
//#endif
		return botlib_export->ai.Characteristic_Integer( args[1], args[2] );
	case BOTLIB_AI_CHARACTERISTIC_BINTEGER:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHARACTERISTIC_BINTEGER");
//#endif
		return botlib_export->ai.Characteristic_BInteger( args[1], args[2], args[3], args[4] );
	case BOTLIB_AI_CHARACTERISTIC_STRING:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHARACTERISTIC_STRING");
//#endif
		botlib_export->ai.Characteristic_String( args[1], args[2], VMA(3), args[4] );
		return 0;

	case BOTLIB_AI_ALLOC_CHAT_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ALLOC_CHAT_STATE");
//#endif
		return botlib_export->ai.BotAllocChatState();
	case BOTLIB_AI_FREE_CHAT_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FREE_CHAT_STATE");
//#endif
		botlib_export->ai.BotFreeChatState( args[1] );
		return 0;
	case BOTLIB_AI_QUEUE_CONSOLE_MESSAGE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_QUEUE_CONSOLE_MESSAGE");
//#endif
		botlib_export->ai.BotQueueConsoleMessage( args[1], args[2], VMA(3) );
		return 0;
	case BOTLIB_AI_REMOVE_CONSOLE_MESSAGE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_REMOVE_CONSOLE_MESSAGE");
//#endif
		botlib_export->ai.BotRemoveConsoleMessage( args[1], args[2] );
		return 0;
	case BOTLIB_AI_NEXT_CONSOLE_MESSAGE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_NEXT_CONSOLE_MESSAGE");
//#endif
		return botlib_export->ai.BotNextConsoleMessage( args[1], VMA(2) );
	case BOTLIB_AI_NUM_CONSOLE_MESSAGE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_NUM_CONSOLE_MESSAGE");
//#endif
		return botlib_export->ai.BotNumConsoleMessages( args[1] );
	case BOTLIB_AI_INITIAL_CHAT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_INITIAL_CHAT");
//#endif
		botlib_export->ai.BotInitialChat( args[1], VMA(2), args[3], VMA(4), VMA(5), VMA(6), VMA(7), VMA(8), VMA(9), VMA(10), VMA(11) );
		return 0;
	case BOTLIB_AI_NUM_INITIAL_CHATS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_NUM_INITIAL_CHATS");
//#endif
		return botlib_export->ai.BotNumInitialChats( args[1], VMA(2) );
	case BOTLIB_AI_REPLY_CHAT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_REPLY_CHAT");
//#endif
		return botlib_export->ai.BotReplyChat( args[1], VMA(2), args[3], args[4], VMA(5), VMA(6), VMA(7), VMA(8), VMA(9), VMA(10), VMA(11), VMA(12) );
	case BOTLIB_AI_CHAT_LENGTH:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHAT_LENGTH");
//#endif
		return botlib_export->ai.BotChatLength( args[1] );
	case BOTLIB_AI_ENTER_CHAT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ENTER_CHAT");
//#endif
		botlib_export->ai.BotEnterChat( args[1], args[2], args[3] );
		return 0;
	case BOTLIB_AI_GET_CHAT_MESSAGE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_CHAT_MESSAGE");
//#endif
		botlib_export->ai.BotGetChatMessage( args[1], VMA(2), args[3] );
		return 0;
	case BOTLIB_AI_STRING_CONTAINS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_STRING_CONTAINS");
//#endif
		return botlib_export->ai.StringContains( VMA(1), VMA(2), args[3] );
	case BOTLIB_AI_FIND_MATCH:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FIND_MATCH");
//#endif
		return botlib_export->ai.BotFindMatch( VMA(1), VMA(2), args[3] );
	case BOTLIB_AI_MATCH_VARIABLE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_MATCH_VARIABLE");
//#endif
		botlib_export->ai.BotMatchVariable( VMA(1), args[2], VMA(3), args[4] );
		return 0;
	case BOTLIB_AI_UNIFY_WHITE_SPACES:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_UNIFY_WHITE_SPACES");
//#endif
		botlib_export->ai.UnifyWhiteSpaces( VMA(1) );
		return 0;
	case BOTLIB_AI_REPLACE_SYNONYMS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_REPLACE_SYNONYMS");
//#endif
		botlib_export->ai.BotReplaceSynonyms( VMA(1), args[2] );
		return 0;
	case BOTLIB_AI_LOAD_CHAT_FILE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_LOAD_CHAT_FILE");
//#endif
		return botlib_export->ai.BotLoadChatFile( args[1], VMA(2), VMA(3) );
	case BOTLIB_AI_SET_CHAT_GENDER:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_SET_CHAT_GENDER");
//#endif
		botlib_export->ai.BotSetChatGender( args[1], args[2] );
		return 0;
	case BOTLIB_AI_SET_CHAT_NAME:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_SET_CHAT_NAME");
//#endif
		botlib_export->ai.BotSetChatName( args[1], VMA(2), args[3] );
		return 0;

	case BOTLIB_AI_RESET_GOAL_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_RESET_GOAL_STATE");
//#endif
		botlib_export->ai.BotResetGoalState( args[1] );
		return 0;
	case BOTLIB_AI_RESET_AVOID_GOALS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_RESET_AVOID_GOALS");
//#endif
		botlib_export->ai.BotResetAvoidGoals( args[1] );
		return 0;
	case BOTLIB_AI_REMOVE_FROM_AVOID_GOALS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_REMOVE_FROM_AVOID_GOALS");
//#endif
		botlib_export->ai.BotRemoveFromAvoidGoals( args[1], args[2] );
		return 0;
	case BOTLIB_AI_PUSH_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_PUSH_GOAL");
//#endif
		botlib_export->ai.BotPushGoal( args[1], VMA(2) );
		return 0;
	case BOTLIB_AI_POP_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_POP_GOAL");
//#endif
		botlib_export->ai.BotPopGoal( args[1] );
		return 0;
	case BOTLIB_AI_EMPTY_GOAL_STACK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_EMPTY_GOAL_STACK");
//#endif
		botlib_export->ai.BotEmptyGoalStack( args[1] );
		return 0;
	case BOTLIB_AI_DUMP_AVOID_GOALS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_DUMP_AVOID_GOALS");
//#endif
		botlib_export->ai.BotDumpAvoidGoals( args[1] );
		return 0;
	case BOTLIB_AI_DUMP_GOAL_STACK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_DUMP_GOAL_STACK");
//#endif
		botlib_export->ai.BotDumpGoalStack( args[1] );
		return 0;
	case BOTLIB_AI_GOAL_NAME:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GOAL_NAME");
//#endif
		botlib_export->ai.BotGoalName( args[1], VMA(2), args[3] );
		return 0;
	case BOTLIB_AI_GET_TOP_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_TOP_GOAL");
//#endif
		return botlib_export->ai.BotGetTopGoal( args[1], VMA(2) );
	case BOTLIB_AI_GET_SECOND_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_SECOND_GOAL");
//#endif
		return botlib_export->ai.BotGetSecondGoal( args[1], VMA(2) );
	case BOTLIB_AI_CHOOSE_LTG_ITEM:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHOOSE_LTG_ITEM");
//#endif
		return botlib_export->ai.BotChooseLTGItem( args[1], VMA(2), VMA(3), args[4] );
	case BOTLIB_AI_CHOOSE_NBG_ITEM:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHOOSE_NBG_ITEM");
//#endif
		return botlib_export->ai.BotChooseNBGItem( args[1], VMA(2), VMA(3), args[4], VMA(5), VMF(6) );
	case BOTLIB_AI_TOUCHING_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_TOUCHING_GOAL");
//#endif
		return botlib_export->ai.BotTouchingGoal( VMA(1), VMA(2) );
	case BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE");
//#endif
		return botlib_export->ai.BotItemGoalInVisButNotVisible( args[1], VMA(2), VMA(3), VMA(4) );
	case BOTLIB_AI_GET_LEVEL_ITEM_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_LEVEL_ITEM_GOAL");
//#endif
		return botlib_export->ai.BotGetLevelItemGoal( args[1], VMA(2), VMA(3) );
	case BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL");
//#endif
		return botlib_export->ai.BotGetNextCampSpotGoal( args[1], VMA(2) );
	case BOTLIB_AI_GET_MAP_LOCATION_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_MAP_LOCATION_GOAL");
//#endif
		return botlib_export->ai.BotGetMapLocationGoal( VMA(1), VMA(2) );
	case BOTLIB_AI_AVOID_GOAL_TIME:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_AVOID_GOAL_TIME");
//#endif
		return FloatAsInt( botlib_export->ai.BotAvoidGoalTime( args[1], args[2] ) );
	case BOTLIB_AI_SET_AVOID_GOAL_TIME:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_SET_AVOID_GOAL_TIME");
//#endif
		botlib_export->ai.BotSetAvoidGoalTime( args[1], args[2], VMF(3));
		return 0;
	case BOTLIB_AI_INIT_LEVEL_ITEMS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_INIT_LEVEL_ITEMS");
//#endif
		botlib_export->ai.BotInitLevelItems();
		return 0;
	case BOTLIB_AI_UPDATE_ENTITY_ITEMS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_UPDATE_ENTITY_ITEMS");
//#endif
		botlib_export->ai.BotUpdateEntityItems();
		return 0;
	case BOTLIB_AI_LOAD_ITEM_WEIGHTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_LOAD_ITEM_WEIGHTS");
//#endif
		return botlib_export->ai.BotLoadItemWeights( args[1], VMA(2) );
	case BOTLIB_AI_FREE_ITEM_WEIGHTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FREE_ITEM_WEIGHTS");
//#endif
		botlib_export->ai.BotFreeItemWeights( args[1] );
		return 0;
	case BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC");
//#endif
		botlib_export->ai.BotInterbreedGoalFuzzyLogic( args[1], args[2], args[3] );
		return 0;
	case BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC");
//#endif
		botlib_export->ai.BotSaveGoalFuzzyLogic( args[1], VMA(2) );
		return 0;
	case BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC");
//#endif
		botlib_export->ai.BotMutateGoalFuzzyLogic( args[1], VMF(2) );
		return 0;
	case BOTLIB_AI_ALLOC_GOAL_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ALLOC_GOAL_STATE");
//#endif
		return botlib_export->ai.BotAllocGoalState( args[1] );
	case BOTLIB_AI_FREE_GOAL_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FREE_GOAL_STATE");
//#endif
		botlib_export->ai.BotFreeGoalState( args[1] );
		return 0;

	case BOTLIB_AI_RESET_MOVE_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_RESET_MOVE_STATE");
//#endif
		botlib_export->ai.BotResetMoveState( args[1] );
		return 0;
	case BOTLIB_AI_ADD_AVOID_SPOT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ADD_AVOID_SPOT");
//#endif
		botlib_export->ai.BotAddAvoidSpot( args[1], VMA(2), VMF(3), args[4] );
		return 0;
	case BOTLIB_AI_MOVE_TO_GOAL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_MOVE_TO_GOAL");
//#endif
		botlib_export->ai.BotMoveToGoal( VMA(1), args[2], VMA(3), args[4] );
		return 0;
	case BOTLIB_AI_MOVE_IN_DIRECTION:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_MOVE_IN_DIRECTION");
//#endif
		return botlib_export->ai.BotMoveInDirection( args[1], VMA(2), VMF(3), args[4] );
	case BOTLIB_AI_RESET_AVOID_REACH:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_RESET_AVOID_REACH");
//#endif
		botlib_export->ai.BotResetAvoidReach( args[1] );
		return 0;
	case BOTLIB_AI_RESET_LAST_AVOID_REACH:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_RESET_LAST_AVOID_REACH");
//#endif
		botlib_export->ai.BotResetLastAvoidReach( args[1] );
		return 0;
	case BOTLIB_AI_REACHABILITY_AREA:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_REACHABILITY_AREA");
//#endif
		return botlib_export->ai.BotReachabilityArea( VMA(1), args[2] );
	case BOTLIB_AI_MOVEMENT_VIEW_TARGET:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_MOVEMENT_VIEW_TARGET");
//#endif
		return botlib_export->ai.BotMovementViewTarget( args[1], VMA(2), args[3], VMF(4), VMA(5) );
	case BOTLIB_AI_PREDICT_VISIBLE_POSITION:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_PREDICT_VISIBLE_POSITION");
//#endif
		return botlib_export->ai.BotPredictVisiblePosition( VMA(1), args[2], VMA(3), args[4], VMA(5) );
	case BOTLIB_AI_ALLOC_MOVE_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ALLOC_MOVE_STATE");
//#endif
		return botlib_export->ai.BotAllocMoveState();
	case BOTLIB_AI_FREE_MOVE_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FREE_MOVE_STATE");
//#endif
		botlib_export->ai.BotFreeMoveState( args[1] );
		return 0;
	case BOTLIB_AI_INIT_MOVE_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_INIT_MOVE_STATE");
//#endif
		botlib_export->ai.BotInitMoveState( args[1], VMA(2) );
		return 0;

	case BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON");
//#endif
		return botlib_export->ai.BotChooseBestFightWeapon( args[1], VMA(2) );
	case BOTLIB_AI_GET_WEAPON_INFO:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GET_WEAPON_INFO");
//#endif
		botlib_export->ai.BotGetWeaponInfo( args[1], args[2], VMA(3) );
		return 0;
	case BOTLIB_AI_LOAD_WEAPON_WEIGHTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_LOAD_WEAPON_WEIGHTS");
//#endif
		return botlib_export->ai.BotLoadWeaponWeights( args[1], VMA(2) );
	case BOTLIB_AI_ALLOC_WEAPON_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_ALLOC_WEAPON_STATE");
//#endif
		return botlib_export->ai.BotAllocWeaponState();
	case BOTLIB_AI_FREE_WEAPON_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_FREE_WEAPON_STATE");
//#endif
		botlib_export->ai.BotFreeWeaponState( args[1] );
		return 0;
	case BOTLIB_AI_RESET_WEAPON_STATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_RESET_WEAPON_STATE");
//#endif
		botlib_export->ai.BotResetWeaponState( args[1] );
		return 0;

	case BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame", "server", "BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION");
//#endif
		return botlib_export->ai.GeneticParentsAndChildSelection(args[1], VMA(2), VMA(3), VMA(4), VMA(5));

	case TRAP_MEMSET:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "qagame_QVM", "server", "TRAP_MEMSET", args[3]);
//#endif
		Com_Memset( VMA(1), args[2], args[3] );
		return 0;

	case TRAP_MEMCPY:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "qagame_QVM", "server", "TRAP_MEMCPY", args[3]);
//#endif
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return 0;

	case TRAP_STRNCPY:
	{
#ifdef USE_SQLITE3
		char *buf;
		if ((buf = malloc(args[3] + 1)) == NULL) {
			Com_Error(ERR_DROP, "Failed to malloc");
		}
		Q_strncpyz(buf, (const char *)VMA(2), args[3] + 1);
		sql_insert_var_text(sql, "qagame_QVM", "server", "TRAP_STRNCPY", "%d %s", args[1], buf);
		free(buf);
#endif
		strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
	}

	case TRAP_SIN:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "qagame_QVM", "server", "TRAP_SIN", VMF(1));
//#endif
		return FloatAsInt( sin( VMF(1) ) );

	case TRAP_COS:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "qagame_QVM", "server", "TRAP_COS", VMF(1));
//#endif
		return FloatAsInt( cos( VMF(1) ) );

	case TRAP_ATAN2:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "qagame_QVM", "server", "TRAP_ATAN2", "%f %f", VMF(1), VMF(2));
//#endif
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );

	case TRAP_SQRT:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "qagame_QVM", "server", "TRAP_SQRT", VMF(1));
//#endif
		return FloatAsInt( sqrt( VMF(1) ) );

	case TRAP_MATRIXMULTIPLY:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame_QVM", "server", "TRAP_MATRIXMULTIPLY");
//#endif
		MatrixMultiply( VMA(1), VMA(2), VMA(3) );
		return 0;

	case TRAP_ANGLEVECTORS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame_QVM", "server", "TRAP_ANGLEVECTORS");
//#endif
		AngleVectors( VMA(1), VMA(2), VMA(3), VMA(4) );
		return 0;

	case TRAP_PERPENDICULARVECTOR:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "qagame_QVM", "server", "TRAP_PERPENDICULARVECTOR");
//#endif
		PerpendicularVector( VMA(1), VMA(2) );
		return 0;

	case TRAP_FLOOR:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "qagame_QVM", "server", "TRAP_FLOOR", FloatAsInt( floor( VMF(1))));
//#endif
		return FloatAsInt( floor( VMF(1) ) );

	case TRAP_CEIL:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "qagame_QVM", "server", "TRAP_CEIL", FloatAsInt( ceil( VMF(1))));
//#endif
		return FloatAsInt( ceil( VMF(1) ) );


	default:
		Com_Error( ERR_DROP, "Bad game system trap: %ld", (long int) args[0] );
	}
	return 0;
}

/*
===============
SV_ShutdownGameProgs

Called every time a map changes
===============
*/
void SV_ShutdownGameProgs( void ) {
	if ( !gvm ) {
		return;
	}
#ifdef USE_SQLITE3
	sql_insert_int(sql, "server", "qagame_QVM", "GAME_SHUTDOWN", 0);
#endif
	VM_Call( gvm, GAME_SHUTDOWN, qfalse );
	VM_Free( gvm );
	gvm = NULL;
}

/*
==================
SV_InitGameVM

Called for both a full init and a restart
==================
*/
static void SV_InitGameVM( qboolean restart ) {
	int		i;
	int		res;

	// start the entity parsing at the beginning
	sv.entityParsePoint = CM_EntityString();

	// clear all gentity pointers that might still be set from
	// a previous level
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=522
	//   now done before GAME_INIT call
	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		svs.clients[i].gentity = NULL;
	}
	
	// use the current msec count for a random seed
	// init for this gamestate
	res = Com_Milliseconds();
#ifdef USE_SQLITE3
	sql_insert_var_text(sql, "server", "qagame_QVM", "GAME_INIT", "%d %d %d", sv.time, res, restart);
#endif
	VM_Call (gvm, GAME_INIT, sv.time, res, restart);
}



/*
===================
SV_RestartGameProgs

Called on a map_restart, but not on a normal map change
===================
*/
void SV_RestartGameProgs( void ) {
	if ( !gvm ) {
		return;
	}
#ifdef USE_SQLITE3
	sql_insert_int(sql, "server", "qagame_QVM", "GAME_SHUTDOWN", 1);
#endif
	VM_Call( gvm, GAME_SHUTDOWN, qtrue );

	// do a restart instead of a free
#ifdef USE_SQLITE3
	sql_insert_null(sql, "server", "qagame_QVM", "VM_Restart");
#endif
	gvm = VM_Restart(gvm, qtrue);
	if ( !gvm ) {
		Com_Error( ERR_FATAL, "VM_Restart on game failed" );
	}

	SV_InitGameVM( qtrue );
}


/*
===============
SV_InitGameProgs

Called on a normal map change, not on a map_restart
===============
*/
void SV_InitGameProgs( void ) {
	cvar_t	*var;
	//FIXME these are temp while I make bots run in vm
	extern int	bot_enable;

	var = Cvar_Get( "bot_enable", "1", CVAR_LATCH );
	if ( var ) {
		bot_enable = var->integer;
	}
	else {
		bot_enable = 0;
	}

	// load the dll or bytecode
#ifdef USE_SQLITE3
	sql_insert_double(sql, "server", "qagame_QVM", "VM_Create", Cvar_VariableValue("vm_game"));
#endif
	gvm = VM_Create( "qagame", SV_GameSystemCalls, Cvar_VariableValue( "vm_game" ) );
	if ( !gvm ) {
		Com_Error( ERR_FATAL, "VM_Create on game failed" );
	}

	SV_InitGameVM( qfalse );
}


/*
====================
SV_GameCommand

See if the current console command is claimed by the game
====================
*/
qboolean SV_GameCommand( void ) {
	if ( sv.state != SS_GAME ) {
		return qfalse;
	}

#ifdef USE_SQLITE3
	sql_insert_null(sql, "server", "qagame_QVM", "GAME_CONSOLE_COMMAND");
#endif
	return VM_Call( gvm, GAME_CONSOLE_COMMAND );
}


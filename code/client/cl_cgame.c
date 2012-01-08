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
// cl_cgame.c  -- client system interaction with client game

#include "client.h"

#include "../botlib/botlib.h"

#ifdef USE_MUMBLE
#include "libmumblelink.h"
#endif

extern	botlib_export_t	*botlib_export;

extern qboolean loadCamera(const char *name);
extern void startCamera(int time);
extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

/*
====================
CL_GetGameState
====================
*/
void CL_GetGameState( gameState_t *gs ) {
	*gs = cl.gameState;
}

/*
====================
CL_GetGlconfig
====================
*/
void CL_GetGlconfig( glconfig_t *glconfig ) {
	*glconfig = cls.glconfig;
}


/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
void	CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.snap.messageNum;
	*serverTime = cl.snap.serverTime;
}

/*
====================
CL_GetSnapshot
====================
*/
qboolean	CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;
	for ( i = 0 ; i < count ; i++ ) {
		snapshot->entities[i] = 
			cl.parseEntities[ ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ];
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
}

/*
=====================
CL_AddCgameCommand
=====================
*/
void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, NULL );
}

/*
=====================
CL_CgameError
=====================
*/
void CL_CgameError( const char *string ) {
	Com_Error( ERR_DROP, "%s", string );
}


/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;
		
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];
	int argc;

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);
	argc = Cmd_Argc();

	if ( !strcmp( cmd, "disconnect" ) ) {
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=552
		// allow server to indicate why they were disconnected
		if ( argc >= 2 )
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected - %s", Cmd_Argv( 1 ) );
		else
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected" );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make apropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}

/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
	cls.cgameStarted = qfalse;
	if ( !cgvm ) {
		return;
	}
#ifdef USE_SQLITE3
	sql_insert_null(sql, "client", "cgame_QVM", "CG_SHUTDOWN");
#endif
	VM_Call( cgvm, CG_SHUTDOWN );
	VM_Free( cgvm );
	cgvm = NULL;
}

static int	FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}

/*
====================
CL_CgameSystemCalls

The cgame module is making a system call
====================
*/
intptr_t CL_CgameSystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case CG_PRINT:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_PRINT", (const char *)VMA(1));
#endif
		Com_Printf( "%s", (const char*)VMA(1) );
		return 0;
	case CG_ERROR:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_ERROR", (const char *)VMA(1));
#endif
		Com_Error( ERR_DROP, "%s", (const char*)VMA(1) );
		return 0;
	case CG_MILLISECONDS:
	{
		int res = Sys_Milliseconds();
#ifdef USE_SQLITE3
		sql_insert_int(sql, "cgame_QVM", "client", "CG_MILLISECONDS", res);
#endif
		return res;
	}
	case CG_CVAR_REGISTER:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_CVAR_REGISTER", "%s %s %d", (const char *)VMA(2), (const char *)VMA(3), args[4]);
#endif
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4] ); 
		return 0;
	case CG_CVAR_UPDATE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CVAR_UPDATE");
//#endif
		Cvar_Update( VMA(1) );
		return 0;
	case CG_CVAR_SET:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_CVAR_SET", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		Cvar_SetSafe( VMA(1), VMA(2) );
		return 0;
	case CG_CVAR_VARIABLESTRINGBUFFER:
		Cvar_VariableStringBuffer( VMA(1), VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_CVAR_VARIABLESTRINGBUFFER", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		return 0;
	case CG_ARGC:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "cgame_QVM", "client", "CG_ARGC", Cmd_Argc());
#endif
		return Cmd_Argc();
	case CG_ARGV:
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_ARGV", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;
	case CG_ARGS:
		Cmd_ArgsBuffer( VMA(1), args[2] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_ARGS", "%d %s", args[2], (const char *)VMA(1));
#endif
		return 0;
	case CG_FS_FOPENFILE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_FS_FOPENFILE", (const char *)VMA(1));
//#endif
		return FS_FOpenFileByMode( VMA(1), VMA(2), args[3] );
	case CG_FS_READ:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_FS_READ", args[2]);
//#endif
		FS_Read2( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_WRITE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_FS_WRITE", args[2]);
//#endif
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_FCLOSEFILE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_FS_FCLOSEFILE", args[1]);
//#endif
		FS_FCloseFile( args[1] );
		return 0;
	case CG_FS_SEEK:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_FS_SEEK", "%d %d %d", args[1], args[2], args[3]);
//#endif
		return FS_Seek( args[1], args[2], args[3] );
	case CG_SENDCONSOLECOMMAND:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_SENDCONSOLECOMMAND", (const char *)VMA(1));
#endif
		Cbuf_AddText( VMA(1) );
		return 0;
	case CG_ADDCOMMAND:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_ADDCOMMAND", (const char *)VMA(1));
#endif
		CL_AddCgameCommand( VMA(1) );
		return 0;
	case CG_REMOVECOMMAND:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_REMOVECOMMAND", (const char *)VMA(1));
#endif
		Cmd_RemoveCommandSafe( VMA(1) );
		return 0;
	case CG_SENDCLIENTCOMMAND:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_SENDCLIENTCOMMAND", "%s %d", (const char *)VMA(1), 0);
#endif
		CL_AddReliableCommand(VMA(1), qfalse);
		return 0;
	case CG_UPDATESCREEN:
		// this is used during lengthy level loading, so pump message loop
//		Com_EventLoop();	// FIXME: if a server restarts here, BAD THINGS HAPPEN!
// We can't call Com_EventLoop here, a restart will crash and this _does_ happen
// if there is a map change while we are downloading at pk3.
// ZOID
#ifdef USE_SQLITE3
		sql_insert_null(sql, "cgame_QVM", "client", "CG_UPDATESCREEN");
#endif
		SCR_UpdateScreen();
		return 0;
	case CG_CM_LOADMAP:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_CM_LOADMAP", (const char *)VMA(1));
#endif
		CL_CM_LoadMap( VMA(1) );
		return 0;
	case CG_CM_NUMINLINEMODELS:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_CM_NUMINLINEMODELS", CM_NumInlineModels());
//#endif
		return CM_NumInlineModels();
	case CG_CM_INLINEMODEL:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_CM_INLINEMODEL", args[1]);
//#endif
		return CM_InlineModel( args[1] );
	case CG_CM_TEMPBOXMODEL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_TEMPBOXMODEL");
//#endif
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qfalse );
	case CG_CM_TEMPCAPSULEMODEL:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_TEMPCAPSULEMODEL");
//#endif
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qtrue );
	case CG_CM_POINTCONTENTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_POINTCONTENTS");
//#endif
		return CM_PointContents( VMA(1), args[2] );
	case CG_CM_TRANSFORMEDPOINTCONTENTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_TRANSFORMEDPOINTCONTENTS");
//#endif
		return CM_TransformedPointContents( VMA(1), args[2], VMA(3), VMA(4) );
	case CG_CM_BOXTRACE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_BOXTRACE");
//#endif
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qfalse );
		return 0;
	case CG_CM_CAPSULETRACE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_CAPSULETRACE");
//#endif
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qtrue );
		return 0;
	case CG_CM_TRANSFORMEDBOXTRACE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_TRANSFORMEDBOXTRACE");
//#endif
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qfalse );
		return 0;
	case CG_CM_TRANSFORMEDCAPSULETRACE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_TRANSFORMEDCAPSULETRACE");
//#endif
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qtrue );
		return 0;
	case CG_CM_MARKFRAGMENTS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_CM_MARKFRAGMENTS");
//#endif
		return re.MarkFragments( args[1], VMA(2), VMA(3), args[4], VMA(5), args[6], VMA(7) );
	case CG_S_STARTSOUND:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_STARTSOUND", args[4]);
#endif
		S_StartSound( VMA(1), args[2], args[3], args[4] );
		return 0;
	case CG_S_STARTLOCALSOUND:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_STARTLOCALSOUND", args[1]);
//#endif
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case CG_S_CLEARLOOPINGSOUNDS:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_CLEARLOOPINGSOUNDS", args[1]);
//#endif
		S_ClearLoopingSounds(args[1]);
		return 0;
	case CG_S_ADDLOOPINGSOUND:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_ADDLOOPINGSOUNDS", args[4]);
//#endif
		S_AddLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_ADDREALLOOPINGSOUND:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_ADDREALLOOPINGSOUNDS", args[4]);
//#endif
		S_AddRealLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_STOPLOOPINGSOUND:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_STOPLOOPINGSOUND", args[1]);
//#endif
		S_StopLoopingSound( args[1] );
		return 0;
	case CG_S_UPDATEENTITYPOSITION:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_UPDATEENTITYPOSITION", args[1]);
//#endif
		S_UpdateEntityPosition( args[1], VMA(2) );
		return 0;
	case CG_S_RESPATIALIZE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_S_RESPATIALIZE", args[1]);
//#endif
		S_Respatialize( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_REGISTERSOUND:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_S_REGISTERSOUND", "%s %d", (const char *)VMA(1), args[2]);
#endif
		return S_RegisterSound( VMA(1), args[2] );
	case CG_S_STARTBACKGROUNDTRACK:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_S_STARTBACKGROUNDTRACK", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
//#endif
		S_StartBackgroundTrack( VMA(1), VMA(2) );
		return 0;
	case CG_R_LOADWORLDMAP:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_LOADWORLDMAP", (const char *)VMA(1));
#endif
		re.LoadWorld( VMA(1) );
		return 0; 
	case CG_R_REGISTERMODEL:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_REGISTERMODEL", (const char *)VMA(1));
//#endif
		return re.RegisterModel( VMA(1) );
	case CG_R_REGISTERSKIN:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_REGISTERSKIN", (const char *)VMA(1));
//#endif
		return re.RegisterSkin( VMA(1) );
	case CG_R_REGISTERSHADER:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_REGISTERSHADER", (const char *)VMA(1));
//#endif
		return re.RegisterShader( VMA(1) );
	case CG_R_REGISTERSHADERNOMIP:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_REGISTERSHADERNOMIP", (const char *)VMA(1));
//#endif
		return re.RegisterShaderNoMip( VMA(1) );
	case CG_R_REGISTERFONT:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_REGISTERFONT", (const char *)VMA(1));
//#endif
		re.RegisterFont( VMA(1), args[2], VMA(3));
		return 0;
	case CG_R_CLEARSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_CLEARSCENE");
//#endif
		re.ClearScene();
		return 0;
	case CG_R_ADDREFENTITYTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_ADDREFENTITYTOSCENE", (const char *)VMA(1));
//#endif
		re.AddRefEntityToScene( VMA(1) );
		return 0;
	case CG_R_ADDPOLYTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_R_ADDPOLYTOSCENE", args[1]);
//#endif
		re.AddPolyToScene( args[1], args[2], VMA(3), 1 );
		return 0;
	case CG_R_ADDPOLYSTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_R_ADDPOLYSTOSCENE", args[1]);
//#endif
		re.AddPolyToScene( args[1], args[2], VMA(3), args[4] );
		return 0;
	case CG_R_LIGHTFORPOINT:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_LIGHTFORPOINT");
//#endif
		return re.LightForPoint( VMA(1), VMA(2), VMA(3), VMA(4) );
	case CG_R_ADDLIGHTTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_ADDLIGHTTOSCENE");
//#endif
		re.AddLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_ADDADDITIVELIGHTTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_ADDADDITIVELIGHTTOSCENE");
//#endif
		re.AddAdditiveLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_RENDERSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_R_RENDERSCENE", (const char *)VMA(1));
//#endif
		re.RenderScene( VMA(1) );
		return 0;
	case CG_R_SETCOLOR:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_SETCOLOR");
//#endif
		re.SetColor( VMA(1) );
		return 0;
	case CG_R_DRAWSTRETCHPIC:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_DRAWSTRETCHPIC");
//#endif
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;
	case CG_R_MODELBOUNDS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_MODELBOUNDS");
//#endif
		re.ModelBounds( args[1], VMA(2), VMA(3) );
		return 0;
	case CG_R_LERPTAG:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_R_LERPTAG");
//#endif
		return re.LerpTag( VMA(1), args[2], args[3], args[4], VMF(5), VMA(6) );
	case CG_GETGLCONFIG:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_GETGLCONFIG");
//#endif
		CL_GetGlconfig( VMA(1) );
		return 0;
	case CG_GETGAMESTATE:
		CL_GetGameState( VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "cgame_QVM", "client", "CG_GETGAMESTATE", (gameState_t *)VMA(1), sizeof(gameState_t));
#endif
		return 0;
	case CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( VMA(1), VMA(2) );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_GETCURRENTSNAPSHOTNUMBER", "%d %d", (*(int *)VMA(1)), (*(int *)VMA(2)));
#endif
		return 0;
	case CG_GETSNAPSHOT:
	{
		qboolean res = CL_GetSnapshot( args[1], VMA(2) );
#ifdef USE_SQLITE3
		if (res) {
			sql_insert_blob(sql, "cgame_QVM", "client", "CG_GETSNAPSHOT", (snapshot_t *)VMA(2), sizeof(snapshot_t));
		} else {
			sql_insert_null(sql, "cgame_QVM", "client", "CG_GETSNAPSHOT");
		}
#endif
		return res;
	}
	case CG_GETSERVERCOMMAND:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "cgame_QVM", "client", "CG_GETSERVERCOMMAND", clc.serverCommands[ args[1] % ( MAX_RELIABLE_COMMANDS - 1) ]);
#endif
		return CL_GetServerCommand( args[1] );
	case CG_GETCURRENTCMDNUMBER:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "cgame_QVM", "client", "CG_GETCURRENTCMDNUMBER", CL_GetCurrentCmdNumber());
#endif
		return CL_GetCurrentCmdNumber();
	case CG_GETUSERCMD:
	{
		qboolean res = CL_GetUserCmd( args[1], VMA(2) );
#ifdef USE_SQLITE3
		if (res) {
			sql_insert_blob(sql, "cgame_QVM", "client", "CG_GETUSERCMD", (usercmd_t *)VMA(2), sizeof(usercmd_t));
		} else {
			sql_insert_null(sql, "cgame_QVM", "client", "CG_GETUSERCMD");
		}
#endif
		return res;
	}
	case CG_SETUSERCMDVALUE:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_GETUSERCMD", "%d %f", args[1], VMF(2));
#endif
		CL_SetUserCmdValue( args[1], VMF(2) );
		return 0;
	case CG_MEMORY_REMAINING:
	{
		int res = Hunk_MemoryRemaining();
#ifdef USE_SQLITE3
		sql_insert_int(sql, "cgame_QVM", "client", "CG_MEMORY_REMAINING", res);
#endif
		return res;
	}
	case CG_KEY_ISDOWN:
	{
		int res = Key_IsDown( args[1] );
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_KEY_ISDOWN", "%d %d", args[1], res);
//#endif
		return res;
	}
	case CG_KEY_GETCATCHER:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_KEY_GETCATCHER", Key_GetCatcher());
//#endif
		return Key_GetCatcher();
	case CG_KEY_SETCATCHER:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_KEY_SETCATCHER",  args[1] | ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) );
//#endif
		// Don't allow the cgame module to close the console
		Key_SetCatcher( args[1] | ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) );
		return 0;
	case CG_KEY_GETKEY:
	{
		int res = Key_GetKey( VMA(1) );
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_KEY_GETKEY", (const char *)VMA(1), res);
//#endif
		return res;
	}

	case CG_MEMSET:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_MEMSET", args[3]);
//#endif
		Com_Memset( VMA(1), args[2], args[3] );
		return 0;
	case CG_MEMCPY:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_MEMCPY", args[3]);
//#endif
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return 0;
	case CG_STRNCPY:
	{
#ifdef USE_SQLITE3
		char *buf;
		if ((buf = malloc(args[3] + 1)) == NULL) {
			Com_Error(ERR_DROP, "Failed to malloc");
		}
		Q_strncpyz(buf, (const char *)VMA(2), args[3] + 1);
		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_STRNCPY", "%d %s", args[1], buf);
		free(buf);
#endif
		strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
	}
	case CG_SIN:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "cgame_QVM", "client", "CG_SIN", VMF(1));
//#endif
		return FloatAsInt( sin( VMF(1) ) );
	case CG_COS:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "cgame_QVM", "client", "CG_COS", VMF(1));
//#endif
		return FloatAsInt( cos( VMF(1) ) );
	case CG_ATAN2:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_ATAN2", "%f %f", VMF(1), VMF(2));
//#endif
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );
	case CG_SQRT:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "cgame_QVM", "client", "CG_SQRT", VMF(1));
//#endif
		return FloatAsInt( sqrt( VMF(1) ) );
	case CG_FLOOR:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "cgame_QVM", "client", "CG_FLOOR", VMF(1));
//#endif
		return FloatAsInt( floor( VMF(1) ) );
	case CG_CEIL:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "cgame_QVM", "client", "CG_CEIL", VMF(1));
//#endif
		return FloatAsInt( ceil( VMF(1) ) );
	case CG_ACOS:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "cgame_QVM", "client", "CG_ACOS", VMF(1));
//#endif
		return FloatAsInt( Q_acos( VMF(1) ) );

	case CG_PC_ADD_GLOBAL_DEFINE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_PC_ADD_GLOBAL_DEFINE", (const char *)VMA(1));
//#endif
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case CG_PC_LOAD_SOURCE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_PC_LOAD_SOURCE", (const char *)VMA(1));
//#endif
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case CG_PC_FREE_SOURCE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_PC_FREE_SOURCE", args[1]);
//#endif
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case CG_PC_READ_TOKEN:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_PC_READ_TOKEN", args[1]);
//#endif
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case CG_PC_SOURCE_FILE_AND_LINE:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_PC_SOURCE_FILE_AND_LINE", "%d %s %s", args[1], (const char *)VMA(2), (const char *)VMA(3));
//#endif
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case CG_S_STOPBACKGROUNDTRACK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_S_STOPBACKGROUNDTRACK");
//#endif
		S_StopBackgroundTrack();
		return 0;

	case CG_REAL_TIME:
	{
		int res = Com_RealTime( VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_int(sql, "cgame_QVM", "client", "CG_REAL_TIME", res);
#endif
		return res;
	}

	case CG_SNAPVECTOR:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_SNAPVECTOR");
//#endif
		Q_SnapVector(VMA(1));
		return 0;

	case CG_CIN_PLAYCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "cgame_QVM", "client", "CG_CIN_PLAYCINEMATIC", (const char *)VMA(1));
//#endif
		return CIN_PlayCinematic(VMA(1), args[2], args[3], args[4], args[5], args[6]);

	case CG_CIN_STOPCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_CIN_STOPCINEMATIC", args[1]);
//#endif
		return CIN_StopCinematic(args[1]);

	case CG_CIN_RUNCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_CIN_RUNCINEMATIC", args[1]);
//#endif
		return CIN_RunCinematic(args[1]);

	case CG_CIN_DRAWCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_CIN_DRAWCINEMATIC", args[1]);
//#endif
		CIN_DrawCinematic(args[1]);
		return 0;

	case CG_CIN_SETEXTENTS:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "cgame_QVM", "client", "CG_CIN_SETEXTENTS", args[1]);
//#endif
		CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
		return 0;

	case CG_R_REMAP_SHADER:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "cgame_QVM", "client", "CG_R_REMAP_SHADER", "%s %s %s", (const char *)VMA(1), (const char *)VMA(2), (const char *)VMA(3));
//#endif
		re.RemapShader( VMA(1), VMA(2), VMA(3) );
		return 0;

/*
	case CG_LOADCAMERA:
		return loadCamera(VMA(1));

	case CG_STARTCAMERA:
		startCamera(args[1]);
		return 0;

	case CG_GETCAMERAINFO:
		return getCameraInfo(args[1], VMA(2), VMA(3));
*/
	case CG_GET_ENTITY_TOKEN:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_GET_ENTITY_TOKEN");
//#endif
		return re.GetEntityToken( VMA(1), args[2] );
	case CG_R_INPVS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "cgame_QVM", "client", "CG_INPVS");
//#endif
		return re.inPVS( VMA(1), VMA(2) );

	default:
	        assert(0);
		Com_Error( ERR_DROP, "Bad cgame system trap: %ld", (long int) args[0] );
	}
	return 0;
}


/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;
	vmInterpret_t		interpret;

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// load the dll or bytecode
	interpret = Cvar_VariableValue("vm_cgame");
	if(cl_connectedToPureServer)
	{
		// if sv_pure is set we only allow qvms to be loaded
		if(interpret != VMI_COMPILED && interpret != VMI_BYTECODE)
			interpret = VMI_COMPILED;
	}

	cgvm = VM_Create( "cgame", CL_CgameSystemCalls, interpret );
	if ( !cgvm ) {
		Com_Error( ERR_DROP, "VM_Create on cgame failed" );
	}
	clc.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
#ifdef USE_SQLITE3
	sql_insert_var_text(sql, "client", "cgame_QVM", "CG_INIT", "%d %d %d",  clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum );
#endif
	VM_Call( cgvm, CG_INIT, clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum );

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	clc.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
	if (!Sys_LowPhysicalMemory()) {
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cgvm ) {
		return qfalse;
	}

#ifdef USE_SQLITE3
	sql_insert_null(sql, "client", "cgame_QVM", "CG_CONSOLE_COMMAND");
#endif
	return VM_Call( cgvm, CG_CONSOLE_COMMAND );
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
#ifdef USE_SQLITE3
	sql_insert_var_text(sql, "client", "cgame_QVM", "CG_DRAW_ACTIVE_FRAME", "%d %d %d", cl.serverTime, stereo, clc.demoplaying );
#endif
	VM_Call( cgvm, CG_DRAW_ACTIVE_FRAME, cl.serverTime, stereo, clc.demoplaying );
	VM_Debug( 0 );
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}
	clc.state = CA_ACTIVE;

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}

#ifdef USE_MUMBLE
	if ((cl_useMumble->integer) && !mumble_islinked()) {
		int ret = mumble_link(CLIENT_WINDOW_TITLE);
		Com_Printf("Mumble: Linking to Mumble application %s\n", ret==0?"ok":"failed");
	}
#endif

#ifdef USE_VOIP
	if (!clc.speexInitialized) {
		int i;
		speex_bits_init(&clc.speexEncoderBits);
		speex_bits_reset(&clc.speexEncoderBits);

		clc.speexEncoder = speex_encoder_init(&speex_nb_mode);

		speex_encoder_ctl(clc.speexEncoder, SPEEX_GET_FRAME_SIZE,
		                  &clc.speexFrameSize);
		speex_encoder_ctl(clc.speexEncoder, SPEEX_GET_SAMPLING_RATE,
		                  &clc.speexSampleRate);

		clc.speexPreprocessor = speex_preprocess_state_init(clc.speexFrameSize,
		                                                  clc.speexSampleRate);

		i = 1;
		speex_preprocess_ctl(clc.speexPreprocessor,
		                     SPEEX_PREPROCESS_SET_DENOISE, &i);

		i = 1;
		speex_preprocess_ctl(clc.speexPreprocessor,
		                     SPEEX_PREPROCESS_SET_AGC, &i);

		for (i = 0; i < MAX_CLIENTS; i++) {
			speex_bits_init(&clc.speexDecoderBits[i]);
			speex_bits_reset(&clc.speexDecoderBits[i]);
			clc.speexDecoder[i] = speex_decoder_init(&speex_nb_mode);
			clc.voipIgnore[i] = qfalse;
			clc.voipGain[i] = 1.0f;
		}
		clc.speexInitialized = qtrue;
		clc.voipMuteAll = qfalse;
		Cmd_AddCommand ("voip", CL_Voip_f);
		Cvar_Set("cl_voipSendTarget", "spatial");
		Com_Memset(clc.voipTargets, ~0, sizeof(clc.voipTargets));
	}
#endif
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( clc.state != CA_ACTIVE ) {
		if ( clc.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( clc.state != CA_ACTIVE ) {
			return;
		}
	}	

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances

	} else {
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better 
		// smoothness or better responsiveness.
		int tn;
		
		tn = cl_timeNudge->integer;
		if (tn<-30) {
			tn = -30;
		} else if (tn>30) {
			tn = 30;
		}

		cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - 5 ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definately
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		int now = Sys_Milliseconds( );
		int frameDuration;

		if (!clc.timeDemoStart) {
			clc.timeDemoStart = clc.timeDemoLastFrame = now;
			clc.timeDemoMinDuration = INT_MAX;
			clc.timeDemoMaxDuration = 0;
		}

		frameDuration = now - clc.timeDemoLastFrame;
		clc.timeDemoLastFrame = now;

		// Ignore the first measurement as it'll always be 0
		if( clc.timeDemoFrames > 0 )
		{
			if( frameDuration > clc.timeDemoMaxDuration )
				clc.timeDemoMaxDuration = frameDuration;

			if( frameDuration < clc.timeDemoMinDuration )
				clc.timeDemoMinDuration = frameDuration;

			// 255 ms = about 4fps
			if( frameDuration > UCHAR_MAX )
				frameDuration = UCHAR_MAX;

			clc.timeDemoDurations[ ( clc.timeDemoFrames - 1 ) %
				MAX_TIMEDEMO_DURATIONS ] = frameDuration;
		}

		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( clc.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}

}




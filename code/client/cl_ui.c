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

#include "client.h"

#include "../botlib/botlib.h"

extern	botlib_export_t	*botlib_export;

vm_t *uivm;

/*
====================
GetClientState
====================
*/
static void GetClientState( uiClientState_t *state ) {
	state->connectPacketCount = clc.connectPacketCount;
	state->connState = clc.state;
	Q_strncpyz( state->servername, clc.servername, sizeof( state->servername ) );
	Q_strncpyz( state->updateInfoString, cls.updateInfoString, sizeof( state->updateInfoString ) );
	Q_strncpyz( state->messageString, clc.serverMessage, sizeof( state->messageString ) );
	state->clientNum = cl.snap.ps.clientNum;
}

/*
====================
LAN_LoadCachedServers
====================
*/
void LAN_LoadCachedServers( void ) {
	int size;
	fileHandle_t fileIn;
	cls.numglobalservers = cls.numfavoriteservers = 0;
	cls.numGlobalServerAddresses = 0;
	if (FS_SV_FOpenFileRead("servercache.dat", &fileIn)) {
		FS_Read(&cls.numglobalservers, sizeof(int), fileIn);
		FS_Read(&cls.numfavoriteservers, sizeof(int), fileIn);
		FS_Read(&size, sizeof(int), fileIn);
		if (size == sizeof(cls.globalServers) + sizeof(cls.favoriteServers)) {
			FS_Read(&cls.globalServers, sizeof(cls.globalServers), fileIn);
			FS_Read(&cls.favoriteServers, sizeof(cls.favoriteServers), fileIn);
		} else {
			cls.numglobalservers = cls.numfavoriteservers = 0;
			cls.numGlobalServerAddresses = 0;
		}
		FS_FCloseFile(fileIn);
	}
}

/*
====================
LAN_SaveServersToCache
====================
*/
void LAN_SaveServersToCache( void ) {
	int size;
	fileHandle_t fileOut = FS_SV_FOpenFileWrite("servercache.dat");
	FS_Write(&cls.numglobalservers, sizeof(int), fileOut);
	FS_Write(&cls.numfavoriteservers, sizeof(int), fileOut);
	size = sizeof(cls.globalServers) + sizeof(cls.favoriteServers);
	FS_Write(&size, sizeof(int), fileOut);
	FS_Write(&cls.globalServers, sizeof(cls.globalServers), fileOut);
	FS_Write(&cls.favoriteServers, sizeof(cls.favoriteServers), fileOut);
	FS_FCloseFile(fileOut);
}


/*
====================
LAN_ResetPings
====================
*/
static void LAN_ResetPings(int source) {
	int count,i;
	serverInfo_t *servers = NULL;
	count = 0;

	switch (source) {
		case AS_LOCAL :
			servers = &cls.localServers[0];
			count = MAX_OTHER_SERVERS;
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			servers = &cls.globalServers[0];
			count = MAX_GLOBAL_SERVERS;
			break;
		case AS_FAVORITES :
			servers = &cls.favoriteServers[0];
			count = MAX_OTHER_SERVERS;
			break;
	}
	if (servers) {
		for (i = 0; i < count; i++) {
			servers[i].ping = -1;
		}
	}
}

/*
====================
LAN_AddServer
====================
*/
static int LAN_AddServer(int source, const char *name, const char *address) {
	int max, *count, i;
	netadr_t adr;
	serverInfo_t *servers = NULL;
	max = MAX_OTHER_SERVERS;
	count = NULL;

	switch (source) {
		case AS_LOCAL :
			count = &cls.numlocalservers;
			servers = &cls.localServers[0];
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			max = MAX_GLOBAL_SERVERS;
			count = &cls.numglobalservers;
			servers = &cls.globalServers[0];
			break;
		case AS_FAVORITES :
			count = &cls.numfavoriteservers;
			servers = &cls.favoriteServers[0];
			break;
	}
	if (servers && *count < max) {
		NET_StringToAdr( address, &adr, NA_IP );
		for ( i = 0; i < *count; i++ ) {
			if (NET_CompareAdr(servers[i].adr, adr)) {
				break;
			}
		}
		if (i >= *count) {
			servers[*count].adr = adr;
			Q_strncpyz(servers[*count].hostName, name, sizeof(servers[*count].hostName));
			servers[*count].visible = qtrue;
			(*count)++;
			return 1;
		}
		return 0;
	}
	return -1;
}

/*
====================
LAN_RemoveServer
====================
*/
static void LAN_RemoveServer(int source, const char *addr) {
	int *count, i;
	serverInfo_t *servers = NULL;
	count = NULL;
	switch (source) {
		case AS_LOCAL :
			count = &cls.numlocalservers;
			servers = &cls.localServers[0];
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			count = &cls.numglobalservers;
			servers = &cls.globalServers[0];
			break;
		case AS_FAVORITES :
			count = &cls.numfavoriteservers;
			servers = &cls.favoriteServers[0];
			break;
	}
	if (servers) {
		netadr_t comp;
		NET_StringToAdr( addr, &comp, NA_IP );
		for (i = 0; i < *count; i++) {
			if (NET_CompareAdr( comp, servers[i].adr)) {
				int j = i;
				while (j < *count - 1) {
					Com_Memcpy(&servers[j], &servers[j+1], sizeof(servers[j]));
					j++;
				}
				(*count)--;
				break;
			}
		}
	}
}


/*
====================
LAN_GetServerCount
====================
*/
static int LAN_GetServerCount( int source ) {
	switch (source) {
		case AS_LOCAL :
			return cls.numlocalservers;
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			return cls.numglobalservers;
			break;
		case AS_FAVORITES :
			return cls.numfavoriteservers;
			break;
	}
	return 0;
}

/*
====================
LAN_GetLocalServerAddressString
====================
*/
static void LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				Q_strncpyz(buf, NET_AdrToStringwPort( cls.localServers[n].adr) , buflen );
				return;
			}
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				Q_strncpyz(buf, NET_AdrToStringwPort( cls.globalServers[n].adr) , buflen );
				return;
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				Q_strncpyz(buf, NET_AdrToStringwPort( cls.favoriteServers[n].adr) , buflen );
				return;
			}
			break;
	}
	buf[0] = '\0';
}

/*
====================
LAN_GetServerInfo
====================
*/
static void LAN_GetServerInfo( int source, int n, char *buf, int buflen ) {
	char info[MAX_STRING_CHARS];
	serverInfo_t *server = NULL;
	info[0] = '\0';
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.localServers[n];
			}
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.favoriteServers[n];
			}
			break;
	}
	if (server && buf) {
		buf[0] = '\0';
		Info_SetValueForKey( info, "hostname", server->hostName);
		Info_SetValueForKey( info, "mapname", server->mapName);
		Info_SetValueForKey( info, "clients", va("%i",server->clients));
		Info_SetValueForKey( info, "sv_maxclients", va("%i",server->maxClients));
		Info_SetValueForKey( info, "ping", va("%i",server->ping));
		Info_SetValueForKey( info, "minping", va("%i",server->minPing));
		Info_SetValueForKey( info, "maxping", va("%i",server->maxPing));
		Info_SetValueForKey( info, "game", server->game);
		Info_SetValueForKey( info, "gametype", va("%i",server->gameType));
		Info_SetValueForKey( info, "nettype", va("%i",server->netType));
		Info_SetValueForKey( info, "addr", NET_AdrToStringwPort(server->adr));
		Info_SetValueForKey( info, "punkbuster", va("%i", server->punkbuster));
		Info_SetValueForKey( info, "g_needpass", va("%i", server->g_needpass));
		Info_SetValueForKey( info, "g_humanplayers", va("%i", server->g_humanplayers));
		Q_strncpyz(buf, info, buflen);
	} else {
		if (buf) {
			buf[0] = '\0';
		}
	}
}

/*
====================
LAN_GetServerPing
====================
*/
static int LAN_GetServerPing( int source, int n ) {
	serverInfo_t *server = NULL;
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.localServers[n];
			}
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.favoriteServers[n];
			}
			break;
	}
	if (server) {
		return server->ping;
	}
	return -1;
}

/*
====================
LAN_GetServerPtr
====================
*/
static serverInfo_t *LAN_GetServerPtr( int source, int n ) {
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return &cls.localServers[n];
			}
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				return &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return &cls.favoriteServers[n];
			}
			break;
	}
	return NULL;
}

/*
====================
LAN_CompareServers
====================
*/
static int LAN_CompareServers( int source, int sortKey, int sortDir, int s1, int s2 ) {
	int res;
	serverInfo_t *server1, *server2;

	server1 = LAN_GetServerPtr(source, s1);
	server2 = LAN_GetServerPtr(source, s2);
	if (!server1 || !server2) {
		return 0;
	}

	res = 0;
	switch( sortKey ) {
		case SORT_HOST:
			res = Q_stricmp( server1->hostName, server2->hostName );
			break;

		case SORT_MAP:
			res = Q_stricmp( server1->mapName, server2->mapName );
			break;
		case SORT_CLIENTS:
			if (server1->clients < server2->clients) {
				res = -1;
			}
			else if (server1->clients > server2->clients) {
				res = 1;
			}
			else {
				res = 0;
			}
			break;
		case SORT_GAME:
			if (server1->gameType < server2->gameType) {
				res = -1;
			}
			else if (server1->gameType > server2->gameType) {
				res = 1;
			}
			else {
				res = 0;
			}
			break;
		case SORT_PING:
			if (server1->ping < server2->ping) {
				res = -1;
			}
			else if (server1->ping > server2->ping) {
				res = 1;
			}
			else {
				res = 0;
			}
			break;
	}

	if (sortDir) {
		if (res < 0)
			return 1;
		if (res > 0)
			return -1;
		return 0;
	}
	return res;
}

/*
====================
LAN_GetPingQueueCount
====================
*/
static int LAN_GetPingQueueCount( void ) {
	return (CL_GetPingQueueCount());
}

/*
====================
LAN_ClearPing
====================
*/
static void LAN_ClearPing( int n ) {
	CL_ClearPing( n );
}

/*
====================
LAN_GetPing
====================
*/
static void LAN_GetPing( int n, char *buf, int buflen, int *pingtime ) {
	CL_GetPing( n, buf, buflen, pingtime );
}

/*
====================
LAN_GetPingInfo
====================
*/
static void LAN_GetPingInfo( int n, char *buf, int buflen ) {
	CL_GetPingInfo( n, buf, buflen );
}

/*
====================
LAN_MarkServerVisible
====================
*/
static void LAN_MarkServerVisible(int source, int n, qboolean visible ) {
	if (n == -1) {
		int count = MAX_OTHER_SERVERS;
		serverInfo_t *server = NULL;
		switch (source) {
			case AS_LOCAL :
				server = &cls.localServers[0];
				break;
			case AS_MPLAYER:
			case AS_GLOBAL :
				server = &cls.globalServers[0];
				count = MAX_GLOBAL_SERVERS;
				break;
			case AS_FAVORITES :
				server = &cls.favoriteServers[0];
				break;
		}
		if (server) {
			for (n = 0; n < count; n++) {
				server[n].visible = visible;
			}
		}

	} else {
		switch (source) {
			case AS_LOCAL :
				if (n >= 0 && n < MAX_OTHER_SERVERS) {
					cls.localServers[n].visible = visible;
				}
				break;
			case AS_MPLAYER:
			case AS_GLOBAL :
				if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
					cls.globalServers[n].visible = visible;
				}
				break;
			case AS_FAVORITES :
				if (n >= 0 && n < MAX_OTHER_SERVERS) {
					cls.favoriteServers[n].visible = visible;
				}
				break;
		}
	}
}


/*
=======================
LAN_ServerIsVisible
=======================
*/
static int LAN_ServerIsVisible(int source, int n ) {
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return cls.localServers[n].visible;
			}
			break;
		case AS_MPLAYER:
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				return cls.globalServers[n].visible;
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return cls.favoriteServers[n].visible;
			}
			break;
	}
	return qfalse;
}

/*
=======================
LAN_UpdateVisiblePings
=======================
*/
qboolean LAN_UpdateVisiblePings(int source ) {
	return CL_UpdateVisiblePings_f(source);
}

/*
====================
LAN_GetServerStatus
====================
*/
int LAN_GetServerStatus( char *serverAddress, char *serverStatus, int maxLen ) {
	return CL_ServerStatus( serverAddress, serverStatus, maxLen );
}

/*
====================
CL_GetGlConfig
====================
*/
static void CL_GetGlconfig( glconfig_t *config ) {
	*config = cls.glconfig;
}

/*
====================
CL_GetClipboardData
====================
*/
static void CL_GetClipboardData( char *buf, int buflen ) {
	char	*cbd;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		*buf = 0;
		return;
	}

	Q_strncpyz( buf, cbd, buflen );

	Z_Free( cbd );
}

/*
====================
Key_KeynumToStringBuf
====================
*/
static void Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	Q_strncpyz( buf, Key_KeynumToString( keynum ), buflen );
}

/*
====================
Key_GetBindingBuf
====================
*/
static void Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	char	*value;

	value = Key_GetBinding( keynum );
	if ( value ) {
		Q_strncpyz( buf, value, buflen );
	}
	else {
		*buf = 0;
	}
}

/*
====================
CLUI_GetCDKey
====================
*/
static void CLUI_GetCDKey( char *buf, int buflen ) {
#ifndef STANDALONE
	cvar_t	*fs;
	fs = Cvar_Get ("fs_game", "", CVAR_INIT|CVAR_SYSTEMINFO );
	if (UI_usesUniqueCDKey() && fs && fs->string[0] != 0) {
		Com_Memcpy( buf, &cl_cdkey[16], 16);
		buf[16] = 0;
	} else {
		Com_Memcpy( buf, cl_cdkey, 16);
		buf[16] = 0;
	}
#else
	*buf = 0;
#endif
}


/*
====================
CLUI_SetCDKey
====================
*/
#ifndef STANDALONE
static void CLUI_SetCDKey( char *buf ) {
	cvar_t	*fs;
	fs = Cvar_Get ("fs_game", "", CVAR_INIT|CVAR_SYSTEMINFO );
	if (UI_usesUniqueCDKey() && fs && fs->string[0] != 0) {
		Com_Memcpy( &cl_cdkey[16], buf, 16 );
		cl_cdkey[32] = 0;
		// set the flag so the fle will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	} else {
		Com_Memcpy( cl_cdkey, buf, 16 );
		// set the flag so the fle will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	}
}
#endif

/*
====================
GetConfigString
====================
*/
static int GetConfigString(int index, char *buf, int size)
{
	int		offset;

	if (index < 0 || index >= MAX_CONFIGSTRINGS)
		return qfalse;

	offset = cl.gameState.stringOffsets[index];
	if (!offset) {
		if( size ) {
			buf[0] = 0;
		}
		return qfalse;
	}

	Q_strncpyz( buf, cl.gameState.stringData+offset, size);
 
	return qtrue;
}

/*
====================
FloatAsInt
====================
*/
static int FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}

/*
====================
CL_UISystemCalls

The ui module is making a system call
====================
*/
intptr_t CL_UISystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case UI_ERROR:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "ui_QVM", "client", "UI_ERROR", (const char*)VMA(1));
#endif
		Com_Error( ERR_DROP, "%s", (const char*)VMA(1) );
		return 0;

	case UI_PRINT:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "ui_QVM", "client", "UI_PRINT", (const char*)VMA(1));
#endif
		Com_Printf( "%s", (const char*)VMA(1) );
		return 0;

	case UI_MILLISECONDS:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_MILLISECONDS", Sys_Milliseconds());
#endif
		return Sys_Milliseconds();

	case UI_CVAR_REGISTER:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_REGISTER", "%s %s %d", (const char *)VMA(2), (const char *)VMA(3), args[4]);
#endif
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4] ); 
		return 0;

	case UI_CVAR_UPDATE:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_UPDATE", "%d", VMA(1));
//#endif
		Cvar_Update( VMA(1) );
		return 0;

	case UI_CVAR_SET:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_SET", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		Cvar_SetSafe( VMA(1), VMA(2) );
		return 0;

	case UI_CVAR_VARIABLEVALUE:
	{
		int res = FloatAsInt( Cvar_VariableValue( VMA(1) ) );
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_CVAR_VARIABLEVALUE", res);
#endif
		return res;
	}

	case UI_CVAR_VARIABLESTRINGBUFFER:
		Cvar_VariableStringBuffer( VMA(1), VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_VARIABLESTRINGBUFFER", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		return 0;

	case UI_CVAR_SETVALUE:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_SETVALUE", "%s %f", (const char *)VMA(1), (*(float *)VMA(2)));
#endif
		Cvar_SetValueSafe( VMA(1), VMF(2) );
		return 0;

	case UI_CVAR_RESET:
#ifdef USE_SQLITE3
		sql_insert_text(sql, "ui_QVM", "client", "UI_CVAR_RESET", (const char *)VMA(1));
#endif
		Cvar_Reset( VMA(1) );
		return 0;

	case UI_CVAR_CREATE:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_CREATE", "%s %s %d", (const char *)VMA(1), (const char *)VMA(2), args[3]);
#endif
		Cvar_Get( VMA(1), VMA(2), args[3] );
		return 0;

	case UI_CVAR_INFOSTRINGBUFFER:
		Cvar_InfoStringBuffer( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_CVAR_INFOSTRINGBUFFER", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;

	case UI_ARGC:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_ARGC", Cmd_Argc());
#endif
		return Cmd_Argc();

	case UI_ARGV:
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_ARGV", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;

	case UI_CMD_EXECUTETEXT:
		if(args[1] == EXEC_NOW
		&& (!strncmp(VMA(2), "snd_restart", 11)
		|| !strncmp(VMA(2), "vid_restart", 11)
		|| !strncmp(VMA(2), "quit", 5)))
		{
			Com_Printf (S_COLOR_YELLOW "turning EXEC_NOW '%.11s' into EXEC_INSERT\n", (const char*)VMA(2));
			args[1] = EXEC_INSERT;
		}
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
			sql_insert_var_text(sql, "ui_QVM", "client", "UI_CMD_EXECUTETEXT", "%s %s", when, (const char *)VMA(2));
		}
#endif
		Cbuf_ExecuteText( args[1], VMA(2) );
		return 0;

	case UI_FS_FOPENFILE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_FS_FOPENFILE", (const char *)VMA(1));
//#endif
		return FS_FOpenFileByMode( VMA(1), VMA(2), args[3] );

	case UI_FS_READ:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_FS_READ", args[2]);
//#endif
		FS_Read2( VMA(1), args[2], args[3] );
		return 0;

	case UI_FS_WRITE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_FS_WRITE", args[2]);
//#endif
		FS_Write( VMA(1), args[2], args[3] );
		return 0;

	case UI_FS_FCLOSEFILE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_FS_FCLOSEFILE", args[1]);
//#endif
		FS_FCloseFile( args[1] );
		return 0;

	case UI_FS_GETFILELIST:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_FS_GETFILELIST", "%s %s", VMA(1), VMA(2));
//#endif
		return FS_GetFileList( VMA(1), VMA(2), VMA(3), args[4] );

	case UI_FS_SEEK:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_FS_SEEK", "%d %d %d", args[1], args[2], args[3]);
//#endif
		return FS_Seek( args[1], args[2], args[3] );
	
	case UI_R_REGISTERMODEL:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_R_REGISTERMODEL", (const char *)VMA(1));
//#endif
		return re.RegisterModel( VMA(1) );

	case UI_R_REGISTERSKIN:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_R_REGISTERSKIN", (const char *)VMA(1));
//#endif
		return re.RegisterSkin( VMA(1) );

	case UI_R_REGISTERSHADERNOMIP:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_R_REGISTERSHADERNOMIP", (const char *)VMA(1));
//#endif
		return re.RegisterShaderNoMip( VMA(1) );

	case UI_R_CLEARSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_R_CLEARSCENE");
//#endif
		re.ClearScene();
		return 0;

	case UI_R_ADDREFENTITYTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_R_ADDREFENTITYTOSCENE", (const char *)VMA(1));
//#endif
		re.AddRefEntityToScene( VMA(1) );
		return 0;

	case UI_R_ADDPOLYTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_R_ADDPOLYTOSCENE", args[1]);
//#endif
		re.AddPolyToScene( args[1], args[2], VMA(3), 1 );
		return 0;

	case UI_R_ADDLIGHTTOSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_R_ADDPOLYSTOSCENE", args[1]);
//#endif
		re.AddLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;

	case UI_R_RENDERSCENE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_R_RENDERSCENE", (const char *)VMA(1));
//#endif
		re.RenderScene( VMA(1) );
		return 0;

	case UI_R_SETCOLOR:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_R_SETCOLOR");
//#endif
		re.SetColor( VMA(1) );
		return 0;

	case UI_R_DRAWSTRETCHPIC:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_R_DRAWSTRETCHPIC");
//#endif
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;

	case UI_R_MODELBOUNDS:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_R_MODELBOUNDS");
//#endif
		re.ModelBounds( args[1], VMA(2), VMA(3) );
		return 0;

	case UI_UPDATESCREEN:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "ui_QVM", "client", "UI_UPDATESCREEN");
#endif
		SCR_UpdateScreen();
		return 0;

	case UI_CM_LERPTAG:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_CM_LERPTAG");
//#endif
		re.LerpTag( VMA(1), args[2], args[3], args[4], VMF(5), VMA(6) );
		return 0;

	case UI_S_REGISTERSOUND:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_S_REGISTERSOUND", "%s %d", (const char *)VMA(1), args[2]);
#endif
		return S_RegisterSound( VMA(1), args[2] );

	case UI_S_STARTLOCALSOUND:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_S_STARTLOCALSOUND", args[1]);
//#endif
		S_StartLocalSound( args[1], args[2] );
		return 0;

	case UI_KEY_KEYNUMTOSTRINGBUF:
		Key_KeynumToStringBuf( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_KEY_KEYNUMTOSTRINGBUF", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;

	case UI_KEY_GETBINDINGBUF:
		Key_GetBindingBuf( args[1], VMA(2), args[3] );
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_KEY_GETBINDINGBUF", "%d %s", args[1], (const char *)VMA(2));
//#endif
		return 0;

	case UI_KEY_SETBINDING:
		Key_SetBinding( args[1], VMA(2) );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_KEY_SETBINDING", "%d %s", args[1], (const char *)VMA(2));
#endif
		return 0;

	case UI_KEY_ISDOWN:
	{
		qboolean res = Key_IsDown( args[1] );
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_KEY_ISDOWN", "%d %d", args[1], res);
//#endif
		return res;
	}

	case UI_KEY_GETOVERSTRIKEMODE:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_KEY_GETOVERSTRIKEMODE", Key_GetOverstrikeMode());
#endif
		return Key_GetOverstrikeMode();

	case UI_KEY_SETOVERSTRIKEMODE:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_KEY_SETOVERSTRIKEMODE", args[1]);
#endif
		Key_SetOverstrikeMode( args[1] );
		return 0;

	case UI_KEY_CLEARSTATES:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "ui_QVM", "client", "UI_KEY_CLEARSTATES");
#endif
		Key_ClearStates();
		return 0;

	case UI_KEY_GETCATCHER:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_KEY_GETCATCHER", Key_GetCatcher());
//#endif
		return Key_GetCatcher();

	case UI_KEY_SETCATCHER:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_KEY_SETCATCHER", args[1] | ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ));
//#endif
		// Don't allow the ui module to close the console
		Key_SetCatcher( args[1] | ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) );
		return 0;

	case UI_GETCLIPBOARDDATA:
		CL_GetClipboardData( VMA(1), args[2] );
#ifdef USE_SQLITE3
		sql_insert_text(sql, "ui_QVM", "client", "UI_GETCLIPBOARDDATA", (const char *)VMA(1));
#endif
		return 0;

	case UI_GETCLIENTSTATE:
		GetClientState( VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_blob(sql, "ui_QVM", "client", "UI_GETCLIENTSTATE", (uiClientState_t *)VMA(1), sizeof(uiClientState_t));
#endif
		return 0;		

	case UI_GETGLCONFIG:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_GETGLCONFIG");
//#endif
		CL_GetGlconfig( VMA(1) );
		return 0;

	case UI_GETCONFIGSTRING:
	{
		int res = GetConfigString( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_GETCONFIGSTRING", "%d %s", args[1], (const char *)VMA(2));
#endif
		return res;
	}
	case UI_LAN_LOADCACHEDSERVERS:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "ui_QVM", "client", "UI_LAN_LOADCACHEDSERVERS");
#endif
		LAN_LoadCachedServers();
		return 0;

	case UI_LAN_SAVECACHEDSERVERS:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "ui_QVM", "client", "UI_LAN_SAVECACHEDSERVERS");
#endif
		LAN_SaveServersToCache();
		return 0;

	case UI_LAN_ADDSERVER:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_ADDSERVER", "%d %s %s", args[1], (const char *)VMA(2), (const char *)VMA(3));
#endif
		return LAN_AddServer(args[1], VMA(2), VMA(3));

	case UI_LAN_REMOVESERVER:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_REMOVESERVER", "%d %s", args[1], (const char *)VMA(2));
#endif
		LAN_RemoveServer(args[1], VMA(2));
		return 0;

	case UI_LAN_GETPINGQUEUECOUNT:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_LAN_GETPINGQUEUECOUNT", LAN_GetPingQueueCount());
#endif
		return LAN_GetPingQueueCount();

	case UI_LAN_CLEARPING:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_LAN_CLEARPING", args[1]);
#endif
		LAN_ClearPing( args[1] );
		return 0;

	case UI_LAN_GETPING:
		LAN_GetPing( args[1], VMA(2), args[3], VMA(4) );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_GETPING", "%d %s", (*(int *)VMA(4)), (const char *)VMA(2));
#endif
		return 0;

	case UI_LAN_GETPINGINFO:
		LAN_GetPingInfo( args[1], VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_text(sql, "ui_QVM", "client", "UI_LAN_GETPINGINFO", (const char *)VMA(2));
#endif
		return 0;

	case UI_LAN_GETSERVERCOUNT:
	{
		int res = LAN_GetServerCount(args[1]);
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_GETSERVERCOUNT", "%d %d", args[1], res);
#endif
		return res;
	}

	case UI_LAN_GETSERVERADDRESSSTRING:
		LAN_GetServerAddressString( args[1], args[2], VMA(3), args[4] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_GETSERVERADDRESSSTRING", "%d %s", args[1], (const char *)VMA(3));
#endif
		return 0;

	case UI_LAN_GETSERVERINFO:
		LAN_GetServerInfo( args[1], args[2], VMA(3), args[4] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_GETSERVERINFO", "%d %s", args[1], (const char *)VMA(3));
#endif
		return 0;

	case UI_LAN_GETSERVERPING:
	{
		int res = LAN_GetServerPing( args[1], args[2] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_GETSERVERPING", "%d %d %d", args[1], args[2], res);
#endif
		return res;
	}

	case UI_LAN_MARKSERVERVISIBLE:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_MARKSERVERVISIBLE", "%d %d %d", args[1], args[2], args[3]);
#endif
		LAN_MarkServerVisible( args[1], args[2], args[3] );
		return 0;

	case UI_LAN_SERVERISVISIBLE:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_SERVERISVISIBLE", "%d %d", args[1], args[2]);
#endif
		return LAN_ServerIsVisible( args[1], args[2] );

	case UI_LAN_UPDATEVISIBLEPINGS:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_LAN_UPDATEVISIBLEPINGS", args[1]);
#endif
		return LAN_UpdateVisiblePings( args[1] );

	case UI_LAN_RESETPINGS:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_LAN_RESETPINGS", args[1]);
#endif
		LAN_ResetPings( args[1] );
		return 0;

	case UI_LAN_SERVERSTATUS:
	{
		int res = LAN_GetServerStatus( VMA(1), VMA(2), args[3] );
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_SERVERSTATUS", (const char *)VMA(1), (const char *)VMA(2));
#endif
		return res;
	}

	case UI_LAN_COMPARESERVERS:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_LAN_COMPARESERVERS", "%d %d %d %d %d", args[1], args[2], args[3], args[4], args[5]);
#endif
		return LAN_CompareServers( args[1], args[2], args[3], args[4], args[5] );

	case UI_MEMORY_REMAINING:
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_MEMORY_REMAINING", Hunk_MemoryRemaining());
#endif
		return Hunk_MemoryRemaining();

	case UI_GET_CDKEY:
		CLUI_GetCDKey( VMA(1), args[2] );
#ifdef USE_SQLITE3
		// Don't put the key in here in case someone sends this file out
		sql_insert_text(sql, "ui_QVM", "client", "UI_GET_CDKEY", "key is in VMA(1) but not putting it in here");
#endif
		return 0;

	case UI_SET_CDKEY:
#ifndef STANDALONE
#  ifdef USE_SQLITE3
		// Don't put the key in here in case someone sends this file out
		sql_insert_text(sql, "ui_QVM", "client", "UI_SET_CDKEY", "key is in VMA(1) but not putting it in here");
#  endif
		CLUI_SetCDKey( VMA(1) );
#endif
		return 0;
	
	case UI_SET_PBCLSTATUS:
#ifdef USE_SQLITE3
		sql_insert_null(sql, "ui_QVM", "client", "UI_SET_PBCLSTATUS");
#endif
		return 0;	

	case UI_R_REGISTERFONT:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_R_REGISTERFONT", "%s %d", (const char *)VMA(1), args[2]);
#endif
		re.RegisterFont( VMA(1), args[2], VMA(3));
		return 0;

	case UI_MEMSET:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_MEMSET", args[3]);
//#endif
		Com_Memset( VMA(1), args[2], args[3] );
		return 0;

	case UI_MEMCPY:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_MEMCPY", args[3]);
//#endif
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return 0;

	case UI_STRNCPY:
	{
#ifdef USE_SQLITE3
		char *buf;
		if ((buf = malloc(args[3] + 1)) == NULL) {
			Com_Error(ERR_DROP, "Failed to malloc");
		}
		Q_strncpyz(buf, (const char *)VMA(2), args[3] + 1);
		sql_insert_var_text(sql, "ui_QVM", "client", "CG_STRNCPY", "%d %s", args[1], buf);
		free(buf);
#endif
		strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
	}

	case UI_SIN:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "ui_QVM", "client", "UI_SIN", VMF(1));
//#endif
		return FloatAsInt( sin( VMF(1) ) );

	case UI_COS:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "ui_QVM", "client", "UI_COS", VMF(1));
//#endif
		return FloatAsInt( cos( VMF(1) ) );

	case UI_ATAN2:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_ATAN2", "%f %f", VMF(1), VMF(2));
//#endif
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );

	case UI_SQRT:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "ui_QVM", "client", "UI_SQRT", VMF(1));
//#endif
		return FloatAsInt( sqrt( VMF(1) ) );

	case UI_FLOOR:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "ui_QVM", "client", "UI_FLOOR", VMF(1));
//#endif
		return FloatAsInt( floor( VMF(1) ) );

	case UI_CEIL:
//#ifdef USE_SQLITE3
//		sql_insert_double(sql, "ui_QVM", "client", "UI_CEIL", VMF(1));
//#endif
		return FloatAsInt( ceil( VMF(1) ) );

	case UI_PC_ADD_GLOBAL_DEFINE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_PC_ADD_GLOBAL_DEFINE", (const char *)VMA(1));
//#endif
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case UI_PC_LOAD_SOURCE:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_PC_LOAD_SOURCE", (const char *)VMA(1));
//#endif
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case UI_PC_FREE_SOURCE:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_PC_FREE_SOURCE", args[1]);
//#endif
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case UI_PC_READ_TOKEN:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_PC_READ_TOKEN", args[1]);
//#endif
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case UI_PC_SOURCE_FILE_AND_LINE:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_PC_SOURCE_FILE_AND_LINE", "%d %s %s", args[1], (const char *)VMA(2), (const char *)VMA(3));
//#endif
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case UI_S_STOPBACKGROUNDTRACK:
//#ifdef USE_SQLITE3
//		sql_insert_null(sql, "ui_QVM", "client", "UI_S_STOPBACKGROUNDTRACK");
//#endif
		S_StopBackgroundTrack();
		return 0;
	case UI_S_STARTBACKGROUNDTRACK:
#ifdef USE_SQLITE3
		sql_insert_var_text(sql, "ui_QVM", "client", "UI_S_STARTBACKGROUNDTRACK", "%s %s", (const char *)VMA(1), (const char *)VMA(2));
#endif
		S_StartBackgroundTrack( VMA(1), VMA(2));
		return 0;

	case UI_REAL_TIME:
	{
		int res = Com_RealTime( VMA(1) );
#ifdef USE_SQLITE3
		sql_insert_int(sql, "ui_QVM", "client", "UI_REAL_TIME", res);
#endif
		return res;
	}

	case UI_CIN_PLAYCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_text(sql, "ui_QVM", "client", "UI_CIN_PLAYCINEMATIC", (const char *)VMA(1));
//#endif
	  Com_DPrintf("UI_CIN_PlayCinematic\n");
	  return CIN_PlayCinematic(VMA(1), args[2], args[3], args[4], args[5], args[6]);

	case UI_CIN_STOPCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_CIN_STOPCINEMATIC", args[1]);
//#endif
	  return CIN_StopCinematic(args[1]);

	case UI_CIN_RUNCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_CIN_RUNCINEMATIC", args[1]);
//#endif
	  return CIN_RunCinematic(args[1]);

	case UI_CIN_DRAWCINEMATIC:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_CIN_DRAWCINEMATIC", args[1]);
//#endif
	  CIN_DrawCinematic(args[1]);
	  return 0;

	case UI_CIN_SETEXTENTS:
//#ifdef USE_SQLITE3
//		sql_insert_int(sql, "ui_QVM", "client", "UI_CIN_SETEXTENTS", args[1]);
//#endif
	  CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
	  return 0;

	case UI_R_REMAP_SHADER:
//#ifdef USE_SQLITE3
//		sql_insert_var_text(sql, "ui_QVM", "client", "UI_R_REMAP_SHADER", "%s %s %s", (const char *)VMA(1), (const char *)VMA(2), (const char *)VMA(3));
//#endif
		re.RemapShader( VMA(1), VMA(2), VMA(3) );
		return 0;

	case UI_VERIFY_CDKEY:
#ifdef USE_SQLITE3
		// Don't put this in here in case someone sends this log file out.
		sql_insert_text(sql, "ui_QVM", "client", "UI_VERIFY_CDKEY", "Not going to compare key VMA(1) with VMA(2) checksum");
#endif
		return CL_CDKeyValidate(VMA(1), VMA(2));
		
	default:
		Com_Error( ERR_DROP, "Bad UI system trap: %ld", (long int) args[0] );

	}

	return 0;
}

/*
====================
CL_ShutdownUI
====================
*/
void CL_ShutdownUI( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_UI );
	cls.uiStarted = qfalse;
	if ( !uivm ) {
		return;
	}
#ifdef USE_SQLITE3
	sql_insert_null(sql, "client", "ui_QVM", "UI_SHUTDOWN");
#endif
	VM_Call( uivm, UI_SHUTDOWN );
	VM_Free( uivm );
	uivm = NULL;
}

/*
====================
CL_InitUI
====================
*/
#define UI_OLD_API_VERSION	4

void CL_InitUI( void ) {
	int		v;
	vmInterpret_t		interpret;

	// load the dll or bytecode
	interpret = Cvar_VariableValue("vm_ui");
	if(cl_connectedToPureServer)
	{
		// if sv_pure is set we only allow qvms to be loaded
		if(interpret != VMI_COMPILED && interpret != VMI_BYTECODE)
			interpret = VMI_COMPILED;
	}

#ifdef USE_SQLITE3
	sql_insert_null(sql, "client", "ui_QVM", "VM_Create");
#endif
	uivm = VM_Create( "ui", CL_UISystemCalls, interpret );
	if ( !uivm ) {
		Com_Error( ERR_FATAL, "VM_Create on UI failed" );
	}

	// sanity check
	v = VM_Call( uivm, UI_GETAPIVERSION );
#ifdef USE_SQLITE3
	sql_insert_int(sql, "client", "ui_QVM", "UI_GETAPIVERSION", v);
#endif
	if (v == UI_OLD_API_VERSION) {
//		Com_Printf(S_COLOR_YELLOW "WARNING: loading old Quake III Arena User Interface version %d\n", v );
		// init for this gamestate
#ifdef USE_SQLITE3
	  sql_insert_int(sql, "client", "ui_QVM", "UI_INIT", (clc.state >= CA_AUTHORIZING && clc.state < CA_ACTIVE));
#endif
		VM_Call( uivm, UI_INIT, (clc.state >= CA_AUTHORIZING && clc.state < CA_ACTIVE));
	}
	else if (v != UI_API_VERSION) {
		// Free uivm now, so UI_SHUTDOWN doesn't get called later.
		VM_Free( uivm );
		uivm = NULL;

		Com_Error( ERR_DROP, "User Interface is version %d, expected %d", v, UI_API_VERSION );
		cls.uiStarted = qfalse;
	}
	else {
		// init for this gamestate
#ifdef USE_SQLITE3
	  sql_insert_int(sql, "client", "ui_QVM", "UI_INIT", (clc.state >= CA_AUTHORIZING && clc.state < CA_ACTIVE));
#endif
		VM_Call( uivm, UI_INIT, (clc.state >= CA_AUTHORIZING && clc.state < CA_ACTIVE) );
	}
}

#ifndef STANDALONE
qboolean UI_usesUniqueCDKey( void ) {
	if (uivm) {
#ifdef USE_SQLITE3
		sql_insert_null(sql, "client", "ui_QVM", "UI_HASUNIQUECDKEY");
#endif
		return (VM_Call( uivm, UI_HASUNIQUECDKEY) == qtrue);
	} else {
		return qfalse;
	}
}
#endif

/*
====================
UI_GameCommand

See if the current console command is claimed by the ui
====================
*/
qboolean UI_GameCommand( void ) {
	if ( !uivm ) {
		return qfalse;
	}

#ifdef USE_SQLITE3
	sql_insert_int(sql, "client", "ui_QVM", "UI_CONSOLE_COMMAND", cls.realtime);
#endif
	return VM_Call( uivm, UI_CONSOLE_COMMAND, cls.realtime );
}

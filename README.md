ioq3_sql_log
=============

This is a QVM interaction logger.  It will log the transactions between the
QVM (cgame, ui, qagame) and the client and server.  It also logs the
opposite direction.

It stores everything in a SQLite3 database.  SQLite3 is included in this branch.
Com_Printf is unreliable at higher timescales.  Be sure you use the SQL call
instead.

Many of the SQL inserts are commented out because they are too verbose.  If you
are interested, you can comment/uncomment any of them that you like.  The files
of interest are:

    for cgame QVM:  code/client/cl_cgame.c
    for ui QVM:     code/client/cl_ui.c
    for qagame QVM: code/server/sv_game.c

Be sure to only uncomment what you are interested in.  There are millions of
calls and if you enable something like memset, you will flood the database
with meaningless entries and slow down the client/server.

This will insert full snapshots and gamestate into the SQL database.  It will
grow quite large quickly because of this.

This also builds a sqlite3 executable which you can run queries on the database
when finished.  By default, the name is 'qvm_log.db' for both the client and
server database.  If you run a listen server, it will use the same database
for both components.

Example
-------

Here's an example I pulled from a demo where I grabbed the first 15 seconds of
an Urban Terror match (map load + warmup) from the cgame perspective.  To give
you an idea of the size, it's 50 MB of data although it compresses really well
for storage.

$ ./sqlite3.i386 test_log.db 'SELECT COUNT(id),msgID FROM q3log GROUP BY msgID ORDER BY COUNT(id)'

1|CG_CM_LOADMAP
1|CG_INIT
1|CG_R_LOADWORLDMAP
1|CG_SHUTDOWN
1|gamestate
5|CG_GETGAMESTATE
22|CG_PRINT
26|commandString
28|CG_CVAR_SET
64|CG_UPDATESCREEN
81|CG_ADDCOMMAND
165|CG_S_STARTSOUND
169|CG_CVAR_REGISTER
279|CG_ARGV
458|CG_S_REGISTERSOUND
476|snapshot
478|CG_GETSNAPSHOT
547|CG_STRNCPY
722|CG_GETSERVERCOMMAND
2140|CG_GETCURRENTCMDNUMBER
2142|CG_GETCURRENTSNAPSHOTNUMBER
2142|CG_MILLISECONDS
2205|CG_DRAW_ACTIVE_FRAME
4280|CG_GETUSERCMD
4356|CG_CVAR_VARIABLESTRINGBUFFER
6420|CG_SENDCONSOLECOMMAND
355005|CG_CVAR_UPDATE

In this example, there were too many CG_CVAR_UPDATEs for a default setting so
I disabled it in the code.

Another useful sqlite3 command is hex(column) which will hex escape binary
data from blobs.


TODO
----

The bot commands were regex search/replace based.  I didn't look through the
definitions to see what info I could extract.  I don't plan on uncommenting
them but it should still have proper SQL inserts for someone who is interested
in how the bots work.

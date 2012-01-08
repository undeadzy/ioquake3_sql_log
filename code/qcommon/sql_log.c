#include <stdio.h>
#include <stdlib.h>

#include "q_shared.h"
#include "qcommon.h"

/* Dead simple schema = spreadsheet style
   <autoincrement> <timestamp> <requester> <provider> <type|trap|cmd> <blob or string arg>
*/

sql_data *sql;

/* How many inserts to do before a transaction ends */
#define LOG_TRANSACTION_LIMIT 10000

static int sql_prep_common(sql_data *newSql, const char *caller, const char *target, const char *msgID);

/* If the variable already exists, then increment instance tracking.
   Otherwise, create a new one.

   -1 = failure.  0 = started new database.  > 1 is the reference tracking

   1 is not used so sql_init and sql_close have the same exit codes.
*/
int
sql_init(sql_data **newSql, const char *filename)
{
	if (newSql != NULL && *newSql != NULL) {
		DEBUG_PRINT("This is already an instance");
		(*newSql)->numInstances++;
		return (*newSql)->numInstances;
	}

	if (filename == NULL) {
		DEBUG_PRINT("Passed in a NULL name");
		return -1;
	}

	if (((*newSql) = calloc(sizeof(sql_data), 1)) == NULL) {
		SQL_FAIL((*newSql), "Failed to malloc");
		return -1;
	}

	/* Open the database */
	if (sqlite3_open(filename, &(*newSql)->db) != SQLITE_OK) {
		SQL_FAIL((*newSql), "Failed to open it");
		return -1;
	}

	/* Create a table to store the data */
	if (sqlite3_prepare_v2((*newSql)->db,
	    "CREATE TABLE IF NOT EXISTS q3log"
	    "("
	    "  id INTEGER PRIMARY KEY AUTOINCREMENT," /* alias to rowid */
	    "  tstamp TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%f','now')),"
	    "  caller TEXT NOT NULL,"
	    "  target TEXT NOT NULL,"
	    "  msgid  TEXT NOT NULL,"
	    "  value  BLOB"
	    ")",
	    -1,  &(*newSql)->table, NULL) != SQLITE_OK) {
		SQL_FAIL((*newSql), "Failed to prepare the table statement");
		return -1;
	}

	/* Execute the prepared statement and free it since we don't need it */
	if (sqlite3_step((*newSql)->table) != SQLITE_DONE) {
		SQL_FAIL((*newSql), "Failed to execute the table prepared statement");
		return -1;
	}
	if (sqlite3_finalize((*newSql)->table) != SQLITE_OK) {
		SQL_FAIL((*newSql), "Failed to finalize table prepared statement");
		return -1;
	}
	(*newSql)->table = NULL;

	/* Create SQL statement with placeholders for inserting data */
	if (sqlite3_prepare_v2((*newSql)->db,
	    "INSERT INTO q3log (caller, target, msgid, value)"
	    "VALUES (?, ?, ?, ?)",
	    -1, &(*newSql)->log, NULL) != SQLITE_OK) {
		SQL_FAIL((*newSql), "Failed to prepare the log statement");
		return -1;
	}

	if (sqlite3_prepare_v2((*newSql)->db, "BEGIN TRANSACTION", -1, &(*newSql)->begin, NULL) != SQLITE_OK) {
		SQL_FAIL((*newSql), "Failed to prepare the begin statement");
		return -1;
	}
	if (sqlite3_prepare_v2((*newSql)->db, "END TRANSACTION", -1, &(*newSql)->end, NULL) != SQLITE_OK) {
		SQL_FAIL((*newSql), "Failed to prepare the end statement");
		return -1;
	}
	if (sqlite3_step((*newSql)->begin) != SQLITE_DONE) {
		SQL_FAIL((*newSql), "Failed to execute the begin prepared statement");
		return -1;
	}

	(*newSql)->numInstances = 1;
	return 0;
}

/* Close the global symbol if the reference tracking is 1

   -1 = failure.  0 = closed database.  >= 1 is the reference tracking
*/
int
sql_close(sql_data **newSql)
{
	if (newSql != NULL && *newSql != NULL && (*newSql)->db != NULL) {
		if ((*newSql)->numInstances > 1) {
			(*newSql)->numInstances--;
			DEBUG_PRINT("There are more references out there...");
			return (*newSql)->numInstances;
		}

		return sql_close_local(newSql);

	} else {
		DEBUG_PRINT("Tried to close a NULL pointer");
		return -1;
	}
}

int
sql_close_local(sql_data **newSql)
{
	if (newSql != NULL && *newSql != NULL && (*newSql)->db != NULL) {
		if ((*newSql)->numInstances != 1) {
			DEBUG_PRINT("Call this when there is only one instance left");
			return -1;
		}

		// Must finalize all prepared statements and close BLOB handles
		if ((*newSql)->log != NULL) {
			if (sqlite3_finalize((*newSql)->log) != SQLITE_OK) {
				DEBUG_PRINT("Failed to finalize the log");
			}
			(*newSql)->log = NULL;
		}
		if ((*newSql)->table != NULL) {
			if (sqlite3_finalize((*newSql)->table) != SQLITE_OK) {
				DEBUG_PRINT("Fialed to finalize the table");
			}
			(*newSql)->table = NULL;
		}
		if ((*newSql)->begin != NULL) {
			if (sqlite3_finalize((*newSql)->begin) != SQLITE_OK) {
				DEBUG_PRINT("Fialed to finalize the begin");
			}
			(*newSql)->begin = NULL;
		}
		if ((*newSql)->end != NULL) {
			if (sqlite3_step((*newSql)->end) != SQLITE_DONE) {
				DEBUG_PRINT("Failed to execute the end prepared statement");
			}
			if (sqlite3_finalize((*newSql)->end) != SQLITE_OK) {
				DEBUG_PRINT("Fialed to finalize the end");
			}
			(*newSql)->end = NULL;
		}
		if ((*newSql)->db != NULL) {
			if (sqlite3_close((*newSql)->db) != SQLITE_OK) {
				DEBUG_PRINT("Failed to close the SQLite3 file");
			}

			(*newSql)->db = NULL;
		}

		free(*newSql);
		*newSql = NULL;
		return 0;

	} else {
		DEBUG_PRINT("Tried to close a NULL pointer");
		return -1;
	}
}

int
sql_insert_var_text(sql_data *newSql, const char *caller, const char *target, const char *msgID, const char *msg, ...)
{
	int		next_index;
        va_list         argptr;
        char            text[128 * 1024];

	if (msg == NULL) {
		SQL_FAIL(newSql, "Invalid input");
		return 0;
	}

        va_start (argptr, msg);
        Q_vsnprintf (text, sizeof(text), msg, argptr);
        va_end (argptr);

	if ((next_index = sql_prep_common(newSql, caller, target, msgID)) < 1) {
		SQL_FAIL(newSql, "Failed to prep");
		return 0;
	}

	if (sqlite3_bind_text(newSql->log, next_index, text, strlen(text), SQLITE_TRANSIENT) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	if (sqlite3_step(newSql->log) != SQLITE_DONE) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	return 1;
}

int
sql_insert_null(sql_data *newSql, const char *caller, const char *target, const char *msgID)
{
	int next_index = sql_prep_common(newSql, caller, target, msgID);
	if (next_index < 1) {
		SQL_FAIL(newSql, "Failed to prep");
		return 0;
	}
	if (sqlite3_bind_null(newSql->log, next_index) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	if (sqlite3_step(newSql->log) != SQLITE_DONE) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	return 1;
}

int
sql_insert_double_ptr(sql_data *newSql, const char *caller, const char *target, const char *msgID, double *value)
{
	if (value == NULL) {
		SQL_FAIL(newSql, "Invalid input");
		return 0;
	}

	return sql_insert_double(newSql, caller, target, msgID, *value);
}

int
sql_insert_double(sql_data *newSql, const char *caller, const char *target, const char *msgID, double value)
{
	int next_index;
	next_index = sql_prep_common(newSql, caller, target, msgID);
	if (next_index < 1) {
		SQL_FAIL(newSql, "Failed to prep");
		return 0;
	}
	if (sqlite3_bind_double(newSql->log, next_index, value) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	if (sqlite3_step(newSql->log) != SQLITE_DONE) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	return 1;
}

int
sql_insert_int(sql_data *newSql, const char *caller, const char *target, const char *msgID, int value)
{
	int next_index;

	next_index = sql_prep_common(newSql, caller, target, msgID);
	if (next_index < 1) {
		SQL_FAIL(newSql, "Failed to prep");
		return 0;
	}
	if (sqlite3_bind_int(newSql->log, next_index, value) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	if (sqlite3_step(newSql->log) != SQLITE_DONE) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	return 1;
}

int
sql_insert_text(sql_data *newSql, const char *caller, const char *target, const char *msgID, const char *value)
{
	int next_index = sql_prep_common(newSql, caller, target, msgID);
	if (next_index < 1) {
		SQL_FAIL(newSql, "Failed to prep");
		return 0;
	}

	if (sqlite3_bind_text(newSql->log, next_index, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	if (sqlite3_step(newSql->log) != SQLITE_DONE) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	return 1;
}

int
sql_insert_blob(sql_data *newSql, const char *caller, const char *target, const char *msgID, void *value, int size)
{
	int next_index;
	if (value == NULL && size > 0) {
		SQL_FAIL(newSql, "Invalid input");
		return 0;
	}
	next_index = sql_prep_common(newSql, caller, target, msgID);
	if (next_index < 1) {
		SQL_FAIL(newSql, "Failed to prep");
		return 0;
	}

	if (sqlite3_bind_blob(newSql->log, next_index, value, size, SQLITE_TRANSIENT) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't bind the blob");
		return 0;
	}
	if (sqlite3_step(newSql->log) != SQLITE_DONE) {
		SQL_FAIL(newSql, "Couldn't execute the prepared statement");
		return 0;
	}
	return 1;
}

static int
sql_prep_common(sql_data *newSql, const char *caller, const char *target, const char *msgID)
{
	if (newSql->numInserts > LOG_TRANSACTION_LIMIT) {
		if (sqlite3_step(newSql->end) != SQLITE_DONE) {
			SQL_FAIL(newSql, "Failed to execute the prepared statement");
			return 0;
		}
		if (sqlite3_step(newSql->begin) != SQLITE_DONE) {
			SQL_FAIL(newSql, "Failed to execute the prepared statement");
			return 0;
		}
		newSql->numInserts = 0;
	} else {
		newSql->numInserts++;
	}

	if (sqlite3_reset(newSql->log) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't reset the prepared statement");
		return 0;
	}
	// Don't need this since we are rebinding everything but here for a sanity check
	if (sqlite3_clear_bindings(newSql->log) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't clear the bindings");
		return 0;
	}

	int index = 1;
	if (sqlite3_bind_text(newSql->log, index, caller, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't reset the prepared statement");
		return 0;
	}
	index++;

	if (sqlite3_bind_text(newSql->log, index, target, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't reset the prepared statement");
		return 0;
	}
	index++;

	if (sqlite3_bind_text(newSql->log, index, msgID, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		SQL_FAIL(newSql, "Couldn't reset the prepared statement");
		return 0;
	}
	index++;

	return index;
}

#if 0
int
main(void)
{
	sql_data *dat;

	if (! sql_init(&dat, "test.db")) {
		SQL_FAIL(dat, "Failed to initialize");
		exit(EXIT_FAILURE);
	}

	if (! sql_insert_text(dat, "cgame_QVM", "client", "CG_PRINTF", "test")) {
		SQL_FAIL(dat, "Failed to insert an entry");
		exit(EXIT_FAILURE);
	}

	if (! sql_close(&dat)) {
		SQL_FAIL(dat, "Failed to close");
	}
	return 1;
}
#endif

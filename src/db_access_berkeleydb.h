/*!
 * \file db_access.h
 * \brief Wrapper to access DB functions
 * \author Xavier ETCHEBER
 */

#ifndef MOOWAPP_STATS_DB_ACCESS_H_
#define MOOWAPP_STATS_DB_ACCESS_H_

#include <string>
#include <vector> // Vector of strings

// database
#include <db_cxx.h>

Db * dbw_open(const std::string baseDir, const std::string dbFileName);
std::string dbw_get(Db *db, const std::string strKey, const int flags = 0);
int dbw_add(Db *db, const std::string strKey, const std::string strValue);
void dbw_remove(Db *db, const std::string strKey);
void dbw_flush(Db *db);
void dbw_compact(Db *db);
void dbw_close(Db *db);
void dbw_drop(const char *basedir);

#endif // MOOWAPP_STATS_DB_ACCESS_H_
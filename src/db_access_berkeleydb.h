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

#define VAL_MAX_VALUE_SIZE 100

/*!
 * \class DBAccessBerkeley
 * \brief Class to access DB functions.
 *
 */
class DBAccessBerkeley
{
public:
  bool dbw_open(const std::string baseDir, const std::string dbFileName);
  std::string dbw_get(const std::string strKey, const int flags = 0);
  int dbw_add(const std::string strKey, const std::string strValue);
  void dbw_remove(const std::string strKey);
  void dbw_flush();
  void dbw_compact();
  void dbw_close();
  void dbw_drop(const char *basedir);
 
  // Getter of singleton
  static DBAccessBerkeley &get() throw() {
    return singleton;
  }

private:
  static DBAccessBerkeley singleton;
  Db *bdb; //!< DB pointer
  
  /*!
   * \fn DBAccessBerkeley()
   * \brief Constructor
   */
  DBAccessBerkeley();
  
  // Protection against copy -> Do not define these
  DBAccessBerkeley(const DBAccessBerkeley&);
  void operator=(const DBAccessBerkeley&);
};

#endif // MOOWAPP_STATS_DB_ACCESS_H_
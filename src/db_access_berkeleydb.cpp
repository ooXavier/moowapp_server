/*!
 * \file bdb_access.cpp
 * \brief Wrapper to access DB functions
 * \author Xavier ETCHEBER
 */

#include <string>
#include <vector> // Vector of strings
#include <stdint.h> // uint64_t
#include <string.h> // memset
#include <stdlib.h> // free
 
// database
#include <db_cxx.h>

#include "db_access_berkeleydb.h"

using namespace std;

bool DBAccessBerkeley::dbw_open(const string baseDir, const string bdbFileName) {
  /// Setup the database environment
  u_int32_t env_flags =
    DB_CREATE     |   // If the environment does not exist, create it.
    DB_INIT_LOCK  |   // Initialize locking
    //DB_INIT_LOG   |   // Initialize logging
    DB_INIT_MPOOL |   // Initialize the cache
    DB_PRIVATE    |   // single process
    DB_THREAD;        // free-threaded (thread-safe)
  
  try {
    DbEnv *env = new DbEnv(0);
    env->open(baseDir.c_str(), env_flags, 0);
    env->set_error_stream(&cerr); // Redirect debugging information to std::cerr
    
    /// Open the database
    bdb = new Db(env, 0);
    u_int32_t bdb_flags =
      DB_CREATE     |   // If the bdb does not exist, create it.
      DB_THREAD;        // free-threaded (thread-safe)
    bdb->open(NULL, bdbFileName.c_str(), NULL, DB_BTREE, bdb_flags, 0);
    cout << "DB " << baseDir << bdbFileName << " connected" << endl;
    return true;
  }
  // DbException is not a subclass of std::exception, so we
  // need to catch them both.
  catch(DbException &e) {
    cerr << "Error opening database: " << baseDir << bdbFileName << endl;
    cerr << e.what() << endl;
  } catch(exception &e) {
    cerr << "Error opening database: " << baseDir << bdbFileName << endl;
    cerr << e.what() << endl;
  }
  
  return false;
}

string DBAccessBerkeley::dbw_get(const string strKey, const int flags/* = 0 */) {
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  
  Dbt data;
  if (flags == 0) {
    data.set_flags(DB_DBT_USERMEM);
    char value[VAL_MAX_VALUE_SIZE + 1];
    data.set_data(value);
    data.set_ulen(VAL_MAX_VALUE_SIZE + 1);
  } else {  
    memset(&data, 0, sizeof(data));
    data.set_flags(DB_DBT_MALLOC);
  }

  try {
    /// Get value from DB
    if (bdb->get(NULL, &key, &data, 0) != DB_NOTFOUND) {
      string strRes((const char *)data.get_data(), data.get_size()-1);
      if (flags != 0) {
        free(data.get_data());
      }
      return strRes;
    }
  } catch(DbMemoryException &e) {
    /// DbMemoryException: If value is longer than DEFAULT length, re-try with flag for DB_DBT_MALLOC
    if (flags == 0) {
      return dbw_get(strKey, 1);
    } else {
      cerr << "DB Error DbMemoryException on bdb->get(key=" << strKey << ") with DB_DBT_MALLOC set." << endl;
      cerr << e.what() << endl;
    }
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on bdb->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on bdb->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on bdb->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on bdb->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  }
  return "";
}

int DBAccessBerkeley::dbw_add(const string strKey, const string strValue) { 
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  Dbt data(const_cast<char*>(strValue.data()), strValue.size()+1);
  
  try {
    if (bdb->put(NULL, &key, &data, 0) == 0) {
      return true;
    }
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on bdb->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on bdb->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on bdb->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on bdb->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  }
  return false;
}

void DBAccessBerkeley::dbw_remove(const string strKey) {
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  try {
    bdb->del(NULL, &key, 0);
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on bdb->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on bdb->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on bdb->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on bdb->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  }
  return;
}

void DBAccessBerkeley::dbw_flush() {
  // Flush data
  try {
    if (bdb->sync(0) != 0)
      cout << "DB Flush failed" << endl;
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on bdb->sync()." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on bdb->sync()." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on bdb->sync()." << endl;
    cerr << e.what() << endl;
  }
}

void DBAccessBerkeley::dbw_compact() {
  // Compact bdb 
  try {
    //TODO: Add a configuration variable to do either a simple compact() task or a delete/full re-insert
    if (bdb->compact(NULL, NULL, NULL, NULL, DB_FREE_SPACE, NULL) != 0) {
      cout << "DB Compaction failed" << endl;
    }
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on bdb->compact()." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on bdb->compact()." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on bdb->compact()." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on bdb->compact()." << endl;
    cerr << e.what() << endl;
  }
}

void DBAccessBerkeley::dbw_close() {
  try {
    if (bdb != NULL) {
      // Close the bdb
      bdb->close(0);
    }
  } catch(DbException &e) {
    cerr << "Error closing database." << endl;
    cerr << e.what() << endl;
  } catch(exception &e) {
    cerr << "Error closing database." << endl;
    cerr << e.what() << endl;
  }
}

void DBAccessBerkeley::dbw_drop(const char *basedir) {
  //bdb_drop(basedir);
}

DBAccessBerkeley::DBAccessBerkeley() {
  bdb = NULL;
}

DBAccessBerkeley DBAccessBerkeley::singleton;
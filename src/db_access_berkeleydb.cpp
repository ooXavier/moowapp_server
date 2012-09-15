/*!
 * \file db_access.cpp
 * \brief Wrapper to access DB functions
 * \author Xavier ETCHEBER
 */

#include <string>
#include <vector> // Vector of strings
#include <stdint.h> // uint64_t
 
// database
#include <db_cxx.h>

#define VAL_MAX_VALUE_SIZE 100

using namespace std;

Db * dbw_open(const string baseDir, const string dbFileName) {
  /// Setup the database environment
  u_int32_t env_flags =
    DB_CREATE     |   // If the environment does not exist, create it.
    DB_INIT_LOCK  |   // Initialize locking
    DB_INIT_LOG   |   // Initialize logging
    DB_INIT_MPOOL |   // Initialize the cache
    DB_PRIVATE    |   // single process
    DB_THREAD;        // free-threaded (thread-safe)
  
  try {
    DbEnv *env = new DbEnv(0);
    env->open(baseDir.c_str(), env_flags, 0);
    env->set_error_stream(&cerr); // Redirect debugging information to std::cerr
    
    /// Open the database
    Db *db = new Db(env, 0);
    u_int32_t db_flags =
      DB_CREATE     |   // If the db does not exist, create it.
      DB_THREAD;        // free-threaded (thread-safe)
    db->open(NULL, dbFileName.c_str(), NULL, DB_BTREE, db_flags, 0);
    cout << "DB " << baseDir << dbFileName << " connected" << endl;
    return db;
  }
  // DbException is not a subclass of std::exception, so we
  // need to catch them both.
  catch(DbException &e) {
    cerr << "Error opening database: " << baseDir << dbFileName << endl;
    cerr << e.what() << endl;
  } catch(exception &e) {
    cerr << "Error opening database: " << baseDir << dbFileName << endl;
    cerr << e.what() << endl;
  }
  
  return NULL;
}

string dbw_get(Db *db, const string strKey, const int flags) {
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
    if (db->get(NULL, &key, &data, 0) != DB_NOTFOUND) {
      string strRes((const char *)data.get_data(), data.get_size());
      if (flags != 0) {
        free(data.get_data());
      }
      return strRes;
    }
  } catch(DbMemoryException &e) {
    /// DbMemoryException: If value is longer than DEFAULT length, re-try with flag for DB_DBT_MALLOC
    if (flags == 0) {
      return dbw_get(db, strKey, 1);
    } else {
      cerr << "DB Error DbMemoryException on db->get(key=" << strKey << ") with DB_DBT_MALLOC set." << endl;
      cerr << e.what() << endl;
    }
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on db->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on db->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on db->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on db->get(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  }
  return "";
}

int dbw_add(Db *db, const string strKey, const string strValue) { 
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  Dbt data(const_cast<char*>(strValue.data()), strValue.size()+1);
  
  try {
    if (db->put(NULL, &key, &data, 0) == 0) {
      return true;
    }
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on db->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on db->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on db->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on db->put(key=" << strKey << ", value=" << strValue << ")." << endl;
    cerr << e.what() << endl;
  }
  return false;
}

void dbw_remove(Db *db, const string strKey) {
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  try {
    db->del(NULL, &key, 0);
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on db->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on db->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on db->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on db->del(key=" << strKey << ")." << endl;
    cerr << e.what() << endl;
  }
  return;
}

void dbw_flush(Db *db) {
  // Flush data
  try {
    if (db->sync(0) != 0)
      cout << "DB Flush failed" << endl;
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on db->sync()." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on db->sync()." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on db->sync()." << endl;
    cerr << e.what() << endl;
  }
}

void dbw_compact(Db *db) {
  // Compact db 
  try {
    //TODO: Add a configuration variable to do either a simple compact() task or a delete/full re-insert
    if (db->compact(NULL, NULL, NULL, NULL, DB_FREE_SPACE, NULL) != 0) {
      cout << "DB Compaction failed" << endl;
    }
  } catch(DbDeadlockException &e) {
    cerr << "DB Error DbDeadlockException on db->compact()." << endl;
    cerr << e.what() << endl;
  } catch(DbLockNotGrantedException &e) {
    cerr << "DB Error DbLockNotGrantedException on db->compact()." << endl;
    cerr << e.what() << endl;
  } catch(DbRepHandleDeadException &e) {
    cerr << "DB Error DbRepHandleDeadException on db->compact()." << endl;
    cerr << e.what() << endl;
  } catch(DbException &e) {
    cerr << "DB Error DbException on db->compact()." << endl;
    cerr << e.what() << endl;
  }
}

void dbw_close(Db *db) {
  try {
    if (db != NULL) {
      // Close the db
      db->close(0);
    }
  } catch(DbException &e) {
    cerr << "Error closing database." << endl;
    cerr << e.what() << endl;
  } catch(exception &e) {
    cerr << "Error closing database." << endl;
    cerr << e.what() << endl;
  }
}

void dbw_drop(const char *basedir) {
  //db_drop(basedir);
}
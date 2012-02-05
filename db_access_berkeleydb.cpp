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

#define VAL_LEN 100

using namespace std;

Db * dbw_open(Db *db_, const char *basedir)
{
  try {
    // Redirect debugging information to std::cerr
    db_->set_error_stream(&cerr);
    
    // Open the database
    u_int32_t cFlags_(DB_CREATE);
    db_->open(NULL, basedir, NULL, DB_BTREE, cFlags_, 0);
    cout << "DB connected" << endl;
  }
  // DbException is not a subclass of std::exception, so we
  // need to catch them both.
  catch(DbException &e) {
    cerr << "Error opening database: " << basedir << "\n";
    cerr << e.what() << endl;
    //return false;
  } catch(exception &e) {
    cerr << "Error opening database: " << basedir << "\n";
    cerr << e.what() << endl;
    //return false;
  }
  
  return db_;
}

string dbw_get(Db *db, string strKey)
{
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  Dbt data;
  
  /*char description[VAL_LEN + 1];
  data.set_data(description);
  data.set_ulen(VAL_LEN + 1);
  data.set_flags(DB_DBT_USERMEM);*/
  memset(&data, 0, sizeof(data));

  if (db->get(NULL, &key, &data, 0) == DB_NOTFOUND) {
    //printf("%s\n", "Not found");
    return "";
  } else {
    //printf("%s\n", (char *) data.get_data());
    return (char *) data.get_data();
  }
}

/*vector< pair<string, string> > dbw_get_all(struct nessDB *db)
{
  vector< pair<string, string> > v;
  uint64_t i, keys = 0;
  dbLine *myDb = db_get_all(db, &keys);
  for(i=0; i < keys; i++) {
    v.push_back(make_pair((string) myDb[i].key, (string) myDb[i].val));
    printf("%d/%d - key=%s - value=%s\n", (int) i, (int) keys, myDb[i].key, myDb[i].val );
  }
  return v;
}*/

int dbw_add(Db *db, string strKey, string strValue)
{ 
  Dbt key(const_cast<char*>(strKey.data()), strKey.size());
  Dbt data(const_cast<char*>(strValue.data()), strValue.size()+1);
  
  if (db->put(NULL, &key, &data, 0) == 0)
    return true;
    
  cerr << "DB Error on db->put()." << endl;
  return false;
}

void dbw_remove(Db *db, string strKey)
{
  Dbt key(&strKey, strKey.size());
  db->del(NULL, &key, 0);
}

void dbw_flush(Db *db)
{
  // Flush data
  if (db->sync(0) == 0) 
    cout << "DB Flushed to disk" << endl;
  else
    cout << "DB Flush failed" << endl;
  
  // then compact db 
  if (db->compact(NULL, NULL, NULL, NULL, DB_FREE_SPACE, NULL) == 0) 
    cout << "DB Compaction ended" << endl;
  else
    cout << "DB Compaction failed" << endl;
    
  //TODO: Add a configuration variable to do either a simple compact() taskor a delete/full re-insert
}

void dbw_close(Db *db)
{
  // Close the db
  try {
    db->close(0);
    cout << "DB connection closed" << endl;
  } catch(DbException &e) {
    cerr << "Error closing database: " << e.what() << endl;
  } catch(std::exception &e) {
    cerr << "Error closing database: " << e.what() << endl;
  }
}

void dbw_drop(const char *basedir)
{
  //db_drop(basedir);
}
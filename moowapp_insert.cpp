/*!
 * \file moowapp_insert.cpp
 * \brief Insertion app to fill a little bit the DB for mooWApp
 * \author Xavier ETCHEBER
 */
 
#include <iostream>
#include <string>
#include <set> // Set of modules

// Boost
#include <boost/algorithm/string.hpp> // Split
#include <boost/filesystem.hpp> // includes all needed Boost.Filesystem declarations
#include <boost/progress.hpp> // Timing system

// mooWApp
#include "global.h"
#include "configuration.h"
#include "db_access_berkeleydb.h" //-1.8.1
#include "log_reader.h"

using namespace std;
Db *db;

int main() {
  // Read configuration file
  Config c;
  
  // open the database
  //db = dbw_open(c.DB_BUFFER, c.DB_PATH.c_str());
  Db db_(NULL, 0);
  db = dbw_open(&db_, c.DB_PATH.c_str());
  cout << "Database connected" << endl;

  size_t founds;
  boost::progress_timer t; // start timing
  
  // Reconstruct list of modules
  set<string> setModules;
  set<string>::iterator it;
  string strModules = dbw_get(db, "modules");
  if (strModules.length() > 0) {
    cout << "START > Modules=" << strModules << endl;
    boost::split(setModules, strModules, boost::is_any_of("/"));
  }
  
  // Loop through files to get access_log files
  string fileName;
  boost::filesystem::path dir_path(c.FILTER_PATH);
  if (exists(dir_path)) {
    boost::filesystem::directory_iterator end_itr; // default construction yields past-the-end
    for (boost::filesystem::directory_iterator itr(dir_path) ; itr != end_itr ; ++itr ) {
      if (is_directory(itr->status())) continue;
      
      // For each log file matching name parse lines
      fileName = itr->path().filename().string();
      founds = fileName.find(c.FILTER_SSL);
      if (founds!=string::npos) {
        cout << fileName;
        readLogFile(c, itr->path().string(), setModules);
        cout << endl;
      }
    }
  }
  
  // Update list of modules in DB
  strModules = "";
  for(it=setModules.begin(); it!=setModules.end(); ) {
    strModules += *it + "/";
    it++;
  }
  cout << "NB Modules: " << (int) setModules.size() << endl;
  dbw_remove(db, "modules");
  cout << "Removed" << endl;
  dbw_add(db, "modules", strModules);
  cout << "Re-added" << endl;
  
  // close the database
  cout << "Closing db connection" << endl;
  dbw_close(db);
  
  return 0;
}

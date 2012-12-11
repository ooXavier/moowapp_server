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
#include "db_access_berkeleydb.h"
#include "log_reader.h"

using namespace std;

int main(int argc, char* argv[]) {
  /// Read configuration file
  Config &c = Config::get();
  
  /// Get DB accessor
  DBAccessBerkeley &dbA = DBAccessBerkeley::get();
  
  /// Open the database
  if (! dbA.dbw_open(c.DB_PATH, c.DB_NAME)) {
    cout << "DB not opened. Exit program." << endl;
    return 1;
  }

  size_t founds;
  boost::progress_timer t; // start timing
  
  /// Reconstruct list of modules
  set<string> setModules;
  set<string>::iterator it, itLast;
  string strModules = dbA.dbw_get("modules");
  if (strModules.length() > 0) {
    cout << "START > Modules=" << strModules << endl;
    boost::split(setModules, strModules, boost::is_any_of("/"));
  }
  
  /// Loop through files to get access_log files
  string fileName;
  boost::filesystem::path dir_path(c.FILTER_PATH);
  if (exists(dir_path)) {
    boost::filesystem::directory_iterator end_itr; // default construction yields past-the-end
    for (boost::filesystem::directory_iterator itr(dir_path) ; itr != end_itr ; ++itr ) {
      if (is_directory(itr->status())) continue;
      
      fileName = itr->path().filename().string();
      bool ok = true;
      /// Remove filenames from arguments from files to be analyzed
      for(int i=1; i < argc; i++) {
        if(strcmp(fileName.c_str(), argv[i]) == 0) {
          ok = false;
          break;
        }
      }
      
      /// For each log file matching name parse lines
      founds = fileName.find(c.FILTER_SSL);
      if (ok && founds!=string::npos) {
        cout << "Reading " << fileName << " ..." << flush;
        readLogFile(1, itr->path().string(), setModules);
        cout << " done." << endl;
      }
    }
    } else {
      cerr << "Wrong set up. Directory " << dir_path << " does not exists." << endl;
    }
  
  /// Update list of modules in DB
  strModules = "";
  if (setModules.size() > 0) {
    itLast = --setModules.end();
    for(it=setModules.begin(); it!=setModules.end(); it++) {
      strModules += *it;
      if (it != itLast) {// do not add ending slash to the last item
        strModules += "/";
      }
    }
  }
  cout << "NB Modules: " << (int) setModules.size() << endl;
  dbA.dbw_remove(KEY_MODULES);
  cout << "Removed" << endl;
  dbA.dbw_add(KEY_MODULES, strModules);
  cout << "Re-added" << endl;
  
  /// Close the database
  cout << "Closing db connection" << endl;
  dbA.dbw_close();
  
  return 0;
}

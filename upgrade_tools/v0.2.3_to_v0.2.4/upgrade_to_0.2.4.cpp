/*!
 * \file upgrade_to_0.2.4.cpp
 * \brief Upgrade app to fix DB model's break
 * \author Xavier ETCHEBER
 */
 
#include <iostream>
#include <string>
#include <sstream> // stringstream
#include <set> // Set of modules

// Boost
#include <boost/algorithm/string.hpp> // Split
#include <boost/filesystem.hpp> // includes all needed Boost.Filesystem declarations
#include <boost/progress.hpp> // Timing system
#include <boost/date_time/gregorian/parsers.hpp> // Parser date

// mooWApp
#include "global.h"
#include "configuration.h"
#include "db_access_berkeleydb.h"
#include "log_reader.h"

using namespace std;
Db *db = NULL;

int main(int argc, char* argv[]) {
  ostringstream oss;
  string visit;
  
  /// Get date start and end from args
  if (argc != 2) {
    cerr << "You need two give two dates (inclusive) in parameter as YYYY-MM-DD YYYY-MM-DD" << endl;
    return -1;
  }
  string strDateStart = argv[0];
  string strDateEnd = argv[1];
  
  boost::gregorian::date dateStart(from_string(strDateStart));
  boost::gregorian::date dateEnd(from_string(strDateEnd));
  
  /// Read configuration file
  Config c;
  
  /// Open the database
  db = dbw_open(c.DB_PATH, c.DB_NAME);
  if (db == NULL) {
    cout << "DB not opened. Exit program." << endl;
    return 1;
  }
  
  boost::progress_timer t; // start timing
  
  /// Reconstruct list of modules
  set<string> setModules;
  set<string>::iterator it, itLast;
  string strModules = dbw_get(db, "modules");
  if (strModules.length() > 0) {
    cout << "START > Modules=" << strModules << endl;
    boost::split(setModules, strModules, boost::is_any_of("/"));
  }
  
  /// Loop through dates
  boost::gregorian::day_iterator ditr(dateStart);
  for (;ditr <= dateEnd; ++ditr) {
    /// produces "C: 2011-Nov-04", "C: 2011-Nov-05", ...
    cout << "C: " << to_simple_string(*ditr) << flush;
    
    /// Loop thru modules to compress stored stats
    for(it=setModules.begin(); it!=setModules.end(); it++) {
      for(int lineType = 1; lineType <= 2; lineType++) {
        /// lineType=1 -> URL with return code "200"
        /// lineType=2 -> URL with return code "302"
        /// lineType=3 -> URL with return code "404"
        dayVisit = 0;
        oss << *it << '/' << lineType << '/' << ditr->year() << "-" << setfill('0') << setw(2) << ditr->month()
            << "-" << setfill('0') << setw(2) << ditr->day();
        // Search Key in DB
        visit = dbw_get(db, oss.str());
        if (visit.length() > 0) {
          if(c.DEBUG_LOGS && lineType == 1) cout << "C Found: " << strOss << " =" << visit << "#" << endl;
          /// Delete the current Key in DB
          dbw_remove(db, strOss);
          
          /// Replace by new type as w
          oss.str("");
          oss << *it << "/w/" << lineType << '/' << ditr->year() << "-" << setfill('0') << setw(2) << ditr->month()
              << "-" << setfill('0') << setw(2) << ditr->day();
          dbw_add(db, strOss);
        }
        oss.str("");
      }
    }
  }
  
  /// Update list of modules in DB
  strModules = "";
  itLast = --setModules.end();
  for(it=setModules.begin(); it!=setModules.end(); it++) {
    strModules += *it;
    if (it != itLast) {// do not add ending slash to the last item
      strModules += "/";
    }
  }
  
  /// Close the database
  cout << "Closing db connection" << endl;
  dbw_close(db);
  
  return 0;
}

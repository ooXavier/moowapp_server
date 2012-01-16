/*!
 * \file log_reader.cpp
 * \brief Functions to read web logs files for mooWApp
 * \author Xavier ETCHEBER
 */

#include <iostream>
#include <string>
#include <vector> // Splited string
#include <set> // Set of modules

// Boost
#include <boost/algorithm/string.hpp> // Split
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/parsers.hpp>
#include <boost/spirit/include/karma.hpp> // int to string
#include <boost/lambda/lambda.hpp>
#include <boost/filesystem.hpp> // includes all needed Boost.Filesystem declarations
#include <boost/asio.hpp> // Check ip address

// mooWApp
#include "global.h"
#include "configuration.h"
#include "db_access_berkeleydb.h" // -1.8.1
#include "log_reader.h"

using namespace std;

bool analyseLine(Config c, const string line, set<string> &setModules) {
  if (line.length() < 10) return false; // Line not long enough : error
  SslLog logLine;
  
  // Check the filter in the line before going further
  size_t foundUrl=line.find(c.FILTER_URL1);
  if (foundUrl!=string::npos) {
    logLine.type = "1"; // Keep only URL with -event.do
  } else {
    foundUrl=line.find(c.FILTER_URL2);
    if (foundUrl!=string::npos) {
      logLine.type = "2";// Keep only URL with .do and without -event.do
    } else {
      return false;
    }
  }
  //cout << "line: " << line << endl;
  char buffer[10];
  unsigned int iHour = 0, iMin = 0;
  /*unsigned int day, year;
  char month[4];
  char url[2084];
  char prenom[36];
  char nom[256];
  sscanf(line.c_str(), "%*d.%*d.%*d.%*d - - [%u/%3s/%u:%u:%u:%*d %*c%*d] \"%*s %s HTTP/1.%*c\" %*d %*s %*d %*s %*s %s %s", &day, month, &year, &iHour, &iMin, url, prenom, nom);
  */
  /*cout << "day: " << day << endl;
  cout << "month: " << month << endl;
  cout << "year: " << year << endl;
  cout << "url: " << url << endl;
  cout << "prenom: " << prenom << endl;
  cout << "nom: " << nom << endl;*/
  
  // Split values to parse
  vector<string> strs;
  boost::split(strs, line, boost::is_any_of(" "));
  
  // First data is a true IP with Boost
  ///boost::system::error_code ec;
  ///boost::asio::ip::address::from_string(strs[0], ec);
  ///if (ec) return false;
  
  // Get Date
  ///char s_date[12];
  ///sprintf(s_date, "%d/%s/%d", day, month, year);
  vector<string> datev;
  string ds = strs[3].substr(1, strs[3].length());
  boost::split(datev, ds, boost::is_any_of(":"));
  // Date parsing
  boost::date_time::format_date_parser<boost::gregorian::date, char> parser(format, std::locale("C"));
  boost::date_time::special_values_parser<boost::gregorian::date, char> svp;
  boost::gregorian::date d = parser.parse_date(datev[0], format, svp);
  ///boost::gregorian::date d (year, month, day);///= parser.parse_date(s_date, format, svp);///datev[0], format, svp);
  logLine.date_d = to_iso_extended_string(d); // Save date to YYYY-MM-DD with zeros
  
  // Get Time
  if (datev[1] != "00") datev[1].erase (0, datev[1].find_first_not_of ("0")); // ltrim of leading zeros on hour
  if (datev[2] != "00") datev[2].erase (0, datev[2].find_first_not_of ("0")); // ltrim of leading zeros on minutes
  sscanf(datev[1].c_str(), "%d", &iHour);
  sscanf(datev[2].c_str(), "%d", &iMin);
  boost::spirit::karma::generate(std::back_inserter(logLine.date_t), boost::spirit::karma::int_, (iHour*10) + floor(iMin/10)); // Convert float addition to string
  
  // Get Module
  ///string strUrl(url);
  ///size_t slash = strUrl.find("/", 1);
  size_t slash = strs[6].find("/", 1);
  if (slash == string::npos) return false;
  ///logLine.app = strUrl.substr(1, slash-1);
  logLine.app = strs[6].substr(1, slash-1);
  ///if (c.DEBUG_LOGS) cout << "> Url: " << strUrl;
  if (c.DEBUG_LOGS) cout << "> Url: " << strs[6];
  
  // Set Key
  logLine.logKey = logLine.app+'/'+logLine.type+'/'+logLine.date_d+'/'+logLine.date_t;
  
  if (c.DEBUG_LOGS) cout << " in App: " << logLine.app << " at " << logLine.date_d << " " << logLine.date_t
                         << " as " << logLine.type << " (key set as " << logLine.logKey << ")" << flush;
  
  string val = dbw_get(db, logLine.logKey);
  if (val.length() > 0) { // If Key already exist in db add +1 visit
    if (c.DEBUG_LOGS) cout << " =" << val << " +1 " << endl;
    int iVisit = 0;
    sscanf(val.c_str(), "%d", &iVisit);
    memset(buffer, 0, sizeof buffer);
    sprintf(buffer, "%d", iVisit+1);
    val = buffer;
  } else { // else insert new key with 1 visit  
    val = "1";
    if (c.DEBUG_LOGS) cout << " Set to 1 " << endl;
    
    // Add module in list if not exist
    setModules.insert(logLine.app);
  }
  
  if (!dbw_add(db, logLine.logKey, val))
    cerr << "db.error().name()" << endl;
  
  return true;
}

unsigned long readLogFile(Config c, const string strFile, set<string> &setModules, unsigned long readPos) {
  FILE * pFile = fopen(strFile.c_str(), "rb");
  if (pFile == NULL) {
    cerr << "Error opening file: "<< strFile << endl;
    return 0;
  }
  fseek(pFile, readPos, SEEK_SET);
  
  char line[2048];
  int i = 0; // nb of lines in file
  int myI = 0; // nb of lines matching stg
  unsigned long size;
  
  if (c.DEBUG_LOGS) cout << "> Parsing logs" << endl;
  while(fgets(line, sizeof(line), pFile) != NULL) {
    const string str = line;
    if (analyseLine(c, str, setModules)) myI++;
    i++;
  }
  
  // all of the data has been read; close the file.
  size = ftell(pFile);
  fclose (pFile);
  cout << ">> Visits inserted: " << myI << " from " << i << " lines." << endl;
  
  return size;
}

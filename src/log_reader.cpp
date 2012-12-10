/*!
 * \file log_reader.cpp
 * \brief Functions to read web logs files for mooWApp
 * \author Xavier ETCHEBER
 */

#include <iostream>
#include <string>
#include <vector> // Splited string
#include <set> // Set of modules
#include <stdio.h> // fopen, fseek, ftell, fclose, sscanf, sprintf

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
#include "db_access_berkeleydb.h"
#include "log_reader.h"

using namespace std;

extern Db *db;

/*!
 * \fn string findExtInLine(map<string, set<string> > &mapExtensions, const string &line)
 * \brief Find an extension configured in a line. Return group name if found.
 *
 * \param[in, out] mapExtensions Map of extensions (from config object).
 * \param[in] line to be analysed.
 */
string findExtInLine(map<string, set<string> > &mapExtensions, const string &line) {
  string subLine;
  /// Keep url without args
  size_t qMark = line.find("?");
  if (qMark != string::npos) {
    subLine = line.substr(0, qMark);
  } else {
    subLine = line;
  }
  boost::algorithm::to_lower(subLine); // lower case the url before extension search
  
  /// Search extension in map
  size_t foundExt;
  map<string, set<string> >::iterator itExtMap = mapExtensions.begin();
  set<string>::iterator itExtSet;
  for(; itExtMap!=mapExtensions.end(); itExtMap++) {
    for(itExtSet=(itExtMap->second).begin();itExtSet!=(itExtMap->second).end();itExtSet++) {
      foundExt = subLine.find(*itExtSet);
      /// If extension is found retourn associated group name
      if (foundExt != string::npos) {
        return itExtMap->first;
      }
    }
  }
  /// Return empty string if not found
  return "";
}

/*!
 * \fn void insertLogLine(SslLog &logLine, set<string> &setModules)
 * \brief Insert a new row of visit in stat DB (or do a +1 on an existing line).
 *
 * \param[in] strLog String format of log line to insert in DB.
 */
bool insertLogLine(const string &strLog) {
  string val = dbw_get(db, strLog);
  if (val.length() > 0) { // If Key already exist in db add +1 visit
    DEBUG_LOGS(" =" << val << " +1 ");
    int iVisit = 0;
	  char buffer[10];
    sscanf(val.c_str(), "%d", &iVisit);
    memset(buffer, 0, sizeof buffer);
    sprintf(buffer, "%d", iVisit+1);
    val = buffer;
  } else { // else insert new key with 1 visit  
    val = "1";
    DEBUG_LOGS(" Set to 1 ");
  }

  if (!dbw_add(db, strLog, val))
    cerr << "db.error().name()" << endl;
  
	if (val == "1") return true;
	return false;
}

/*!
 * \fn bool analyseLine(const string line, set<string> &setModules)
 * \brief Create a SslLog object from a line of a log file and call insertLogLine.
 *
 * \param[in] line to be analysed.
 * \param[in, out] setModules set of modules to be updated with the visit inserted in DB.
 */
bool analyseLine(const string &line, set<string> &setModules) {
  if (line.length() < 10) return false; // Line not long enough : error
  SslLog logLine;
  
  // Split values to parse
  vector<string> strs;
  boost::split(strs, line, boost::is_any_of(" "));
  
  /// Get config object containing the path/name of file to read.
  Config c = Config::get();
  
  /// Check the filter in the line before going further
  // first extension
  logLine.group = findExtInLine(c.FILTER_EXTENSION, strs[6]);
  if ((logLine.group).empty()) {
    return false;
  }
  
  // than response code
  size_t foundUrl=line.find(c.FILTER_URL1);
  if (foundUrl!=string::npos) {
    logLine.type = "1"; // Keep only URL with return code " 200 "
  } else {
    foundUrl=line.find(c.FILTER_URL2);
    if ( foundUrl!=string::npos) {
      logLine.type = "2";// Keep only URL with return code " 302 "
    } else {
      /*foundUrl=line.find(c.FILTER_URL3);
      if ((foundExt!=string::npos) && (foundUrl!=string::npos)) {
        logLine.type = "3";// Keep only URL with return code " 404 "
      } else {*/
        return false;
      //}
    }
  }
  
  DEBUG_LOGS(cout << "> Url: " << strs[6]);
  
  //cout << "line: " << line << endl;
  /*unsigned int iHour = 0, iMin = 0;
  unsigned int day, year;
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
  
  // First data is a true IP with Boost
  //boost::system::error_code ec;
  //boost::asio::ip::address::from_string(strs[0], ec);
  //if (ec) return false;
  
  // Get Date
  //char s_date[12];
  //sprintf(s_date, "%d/%s/%d", day, month, year);
  vector<string> datev;
  string ds = strs[3].substr(1, strs[3].length());
  boost::split(datev, ds, boost::is_any_of(":"));
  // Date parsing
  boost::date_time::format_date_parser<boost::gregorian::date, char> parser(format, std::locale("C"));
  boost::date_time::special_values_parser<boost::gregorian::date, char> svp;
  boost::gregorian::date d = parser.parse_date(datev[0], format, svp);
  logLine.date_d = to_iso_extended_string(d); // Save date to YYYY-MM-DD with zeros
  
  // Get Time
  logLine.date_t_minutes = datev[1] + datev[2]; // Minutes mode
  logLine.date_t = datev[1] + datev[2][0]; // 10 Minutes mode
  logLine.date_t_hours = datev[1]; // Hours mode
	
  // Get Module
  size_t slash = strs[6].find("/", 1);
  if (slash == string::npos) return false;
  logLine.app = strs[6].substr(1, slash-1);
  /*if (strs[9] == "-") {
    logLine.responseSize = "0";
  } else {
    logLine.responseSize = strs[9];
  }
  logLine.responseDuration = strs[10];*/
  
  // Set Key
  logLine.logKey = logLine.app+'/'+logLine.group+'/'+logLine.type+'/'+logLine.date_d+'/';
  
  DEBUG_LOGS(" in App: " << logLine.app << " at " << logLine.date_d << " " << logLine.date_t
                         << " as " << logLine.group << " " << logLine.type << " (key set as " << logLine.logKey << logLine.date_t_minutes << ")");
  
  string str = logLine.logKey+logLine.date_t_hours;
  insertLogLine(str);
  str = logLine.logKey+logLine.date_t;
  insertLogLine(str);
  str = logLine.logKey+logLine.date_t_minutes;
  if (insertLogLine(str)) {
    /// Add module in list if not exist
    setModules.insert(logLine.app);
    DEBUG_LOGS(" +1 module for " << logLine.app);
  }

  return true;
}

/*!
 * \fn unsigned long readLogFile(const string strFile, set<string> &setModules, unsigned long readPos)
 * \brief Read a log file and analyse every line starting at a specified position.
 *
 * \param[in] strFile.
 * \param[in, out] setModules set of modules.
 * \param[in] readPos.
 */
unsigned long readLogFile(const string &strFile, set<string> &setModules, unsigned long readPos) {
  char * buffer;
  unsigned long size, lSize;
  FILE * pFile = fopen(strFile.c_str(), "rb");
  if (pFile == NULL) {
    cerr << "Error opening file: " << strFile << endl;
    return 0;
  }
  
  /// obtain file size:
  fseek (pFile, 0, SEEK_END);
  lSize = ftell (pFile);
  if (lSize == readPos) {
    fclose (pFile);
    return lSize;
  }
  lSize -= readPos;
  fseek(pFile, readPos, SEEK_SET);

  /// allocate memory to contain the file:
  buffer = (char*) malloc (sizeof(char) * lSize);
  if (buffer == NULL) {cerr << endl << "Memory error" << endl; return 0;}

  /// copy the file into the memory buffer:
  size_t result = fread (buffer, 1, lSize, pFile);
  if (result != lSize) {cerr << endl << "Reading error" << endl; return 0;}
  
  /// all of the data has been read; close the file.
  size = ftell(pFile);
  fclose (pFile);

  int i = 0; // nb of lines in file
  int myI = 0; // nb of lines matching stg
  
  DEBUG_LOGS("> Parsing logs");
  
  string linedata;
  for (unsigned long j=0; j<lSize; j++) { // loop thru the buffer
    linedata.push_back(buffer[j]); // push character into string
    if(buffer[j] == 13 || buffer[j] == 10) { // until we hit newline
      const string str = linedata;
      if (analyseLine(str, setModules)) myI++;
      i++;
      linedata.clear(); // Clear "temporary string"
    }
  }
  free (buffer);
  
  return size;
}
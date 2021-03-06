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
#include <iomanip> // setw, setfill

// Boost
#include <boost/algorithm/string.hpp> // Split
#include <boost/lexical_cast.hpp> // lexical_cast
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
  size_t dotMark = line.find_first_of(".");
  if (dotMark != string::npos) {
    subLine = line.substr(dotMark); // keeping the dot
    size_t qMark = subLine.find_first_of("?");
    if (qMark != string::npos) {
      subLine = subLine.substr(0, qMark);
    }
  } else {
    // Exit if dot not found in URL
    return "";
  }
  boost::algorithm::to_lower(subLine); // lower case the url before extension search
  //cout << line << " && ext is : " << subLine << endl;
  
  /// Search extension in map
  map<string, set<string> >::iterator itExtMap = mapExtensions.begin();
  set<string>::iterator itExtSet;
  for(; itExtMap!=mapExtensions.end(); itExtMap++) {
    for(itExtSet=(itExtMap->second).begin();itExtSet!=(itExtMap->second).end();itExtSet++) {
      /// If extension is found retourn associated group name
      if (subLine == *itExtSet) {
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
bool insertLogLine(const string &strLog, const string &responseSize, const string &responseDuration) {
  // Get DB accessor
  DBAccessBerkeley &dbA = DBAccessBerkeley::get();
  
  unsigned int iVisit = 0;
  string val = dbA.dbw_get(strLog);
  string newVal = "";
  if (val.length() > 0) {
    /// If Key already exist in db add +1 visit
    DEBUG_LOGS_FUNC("Read: " << strLog << "=" << val);
    /*char buffer[10];
    sscanf(val.c_str(), "%d", &iVisit);
    memset(buffer, 0, sizeof buffer);
    sprintf(buffer, "%d", iVisit++);
    val = buffer;*/
    iVisit = stringToInt(val) + 1;
    intToString(newVal, iVisit);
  } else { /// Else insert new key with 1 visit
    iVisit = 1;
    newVal = "1";
  }
  DEBUG_LOGS_FUNC("Set: " << strLog << "=" << newVal);
  
  if (!dbA.dbw_add(strLog, newVal))
    cerr << "db.error().name()" << endl;

  // Insert response size
  if (responseSize.length() > 0) {
    val = dbA.dbw_get(strLog+"/sz/values");
    if (val.length() > 0) {
      val += "," + responseSize;
    } else {
      val = responseSize;
    }
    DEBUG_LOGS_FUNC("Set: " << strLog << "/sz/values=" << val);
    if (!dbA.dbw_add(strLog+"/sz/values", val))
      cerr << "db.error().name()" << endl;
  }
  
  // Insert response duration
  if (responseDuration.length() > 0) {
    val = dbA.dbw_get(strLog+"/rt/values");
    if (val.length() > 0) {
      val += "," + responseDuration;
    } else {
      val = responseDuration;
    }
    DEBUG_LOGS_FUNC("Set: " << strLog << "/rt/values=" << val);
    if (!dbA.dbw_add(strLog+"/rt/values", val))
      cerr << "db.error().name()" << endl;
  }
  
	if (iVisit == 1) return true;
	return false;
}

/*!
 * \fn bool analyseLine(const string line, set<string> &setModules)
 * \brief Create a SslLog object from a line of a log file and call insertLogLine.
 *
 * \param[in] logFileNb Log file number in configuration (for debugging purpose).
 * \param[in] line to be analysed.
 * \param[in, out] setModules set of modules to be updated with the visit inserted in DB.
 */
bool analyseLine(const unsigned short &logFileNb, const string &line, set<string> &setModules) {
  if (line.length() < 10) return false; // Line not long enough : error
  SslLog logLine;
  
  // Split values to parse
  vector<string> strs;
  boost::split(strs, line, boost::is_any_of(" "));
  
  /// Get config object containing the path/name of file to read.
  Config &c = Config::get();
  
  /// Check the filter in the line before going further
  // first extension
  logLine.group = findExtInLine(c.FILTER_EXTENSION, strs[6]);
  DEBUG_LOGS_FUNC("group is " << logLine.group);
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
  
  DEBUG_LOGS_FUNC("#" << logFileNb << ". Url: " << strs[6]);
  
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
  
  // Get Date and Time
  unsigned int iHour = 0, iMin = 0, day = 0, year = 0;
  char month[4];
  sscanf(strs[3].c_str(), "[%u/%3s/%u:%u:%u:%*d", &day, month, &year, &iHour, &iMin);
  ostringstream oss;
  oss << year << "-" << setw(2) << setfill('0') << getMonth((string) month) << "-" << setw(2) << setfill('0') << day;
  logLine.date_d = oss.str();
  oss.str("");
  // Get Time
  oss << setw(2) << setfill('0') << iHour << setw(2) << setfill('0') << iMin;
  logLine.date_t_minutes = oss.str(); // Minutes mode
  oss.str("");
  oss << setw(2) << setfill('0') << iHour << (int) iMin / 10;
  logLine.date_t = oss.str(); // 10 Minutes mode
  oss.str("");
  oss << setw(2) << setfill('0') << iHour;
  logLine.date_t_hours = oss.str(); // Hours mode
	
  // Get Module
  size_t slash = strs[6].find("/", 1);
  if (slash == string::npos) return false;
  logLine.app = strs[6].substr(1, slash-1);
  if (strs[9] == "-") {
    logLine.responseSize = "0";
  } else {
    logLine.responseSize = strs[9];
  }
  logLine.responseDuration = strs[10];
  
  // Set Key
  logLine.logKey = logLine.app+'/'+logLine.group+'/'+logLine.type+'/'+logLine.date_d+'/';
  
  DEBUG_LOGS_FUNC(logLine.app << " at " << logLine.date_d << " " << logLine.date_t << " as " << logLine.group << " " << logLine.type << " size:" << logLine.responseSize << " in:" << logLine.responseDuration);
  
  string str = logLine.logKey+logLine.date_t_hours;
  const string strEmpty = "";
  insertLogLine(str, strEmpty, strEmpty);
  str = logLine.logKey+logLine.date_t;
  insertLogLine(str, strEmpty, strEmpty);
  str = logLine.logKey+logLine.date_t_minutes;
  if (insertLogLine(str, logLine.responseSize, logLine.responseDuration)) {
    /// Add module in list if not exist
    setModules.insert(logLine.app);
    DEBUG_LOGS_FUNC("+1 module for " << logLine.app);
  }
  
  return true;
}

/*!
 * \fn unsigned long readLogFile(const string strFile, set<string> &setModules, unsigned long readPos)
 * \brief Read a log file and analyse every line starting at a specified position.
 *
 * \param[in] logFileNb Log file number in configuration (for debugging purpose).
 * \param[in] strFile.
 * \param[in, out] setModules set of modules.
 * \param[in] readPos.
 */
uint64_t readLogFile(const unsigned short &logFileNb, const string &strFile, set<string> &setModules, uint64_t readPos) {
  char * buffer;
  uint64_t size, lSize;
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
  
  DEBUG_LOGS_FUNC("#" << logFileNb << ". Parsing logs...");
  
  string linedata;
  for (uint64_t i=0, j=0; j<lSize; j++) { // loop thru the buffer
    linedata.push_back(buffer[j]); // push character into string
    if(buffer[j] == 13 || buffer[j] == 10) { // until we hit newline
      if (i%100 == 0) {
        printProgBar((int) j/(lSize/100));
      }
      analyseLine(logFileNb, linedata, setModules);
      linedata.clear();
      ++i;
    }
  }
  free (buffer);
  printProgBar(100);
  cout << endl;
  
  DEBUG_LOGS_FUNC("#" << logFileNb << ". Done");
  
  return size;
}
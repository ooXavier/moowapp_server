/*!
 * \file configuration.cpp
 * \brief Configuration reader for mooWApp
 * \author Xavier ETCHEBER
 */

#include <iostream>
#include <string>
#include <map> // Conf info

#include "global.h"
#include "configuration.h"

using namespace std;

void Config::trimInfo(string& s) {
  // Remove leading and trailing whitespace
  static const char whitespace[] = " \n\t\v\r\f";
  s.erase( 0, s.find_first_not_of(whitespace) );
  s.erase( s.find_last_not_of(whitespace) + 1U );
}

Config::Config(string cfgFile) {
  typedef string::size_type pos;
  const string delimiter = "=", comment = "#";
  const pos skip = delimiter.length();
  
  map<string, string> mapConf;
  
  FILE * pFile = fopen(cfgFile.c_str(), "rb");
  if (pFile == NULL) {
    cerr << "Error opening configuration file: " << cfgFile << endl;
    cerr << "Make sure there's one in the current directory." << endl;
    throw;
  }
  
  char line[2048];
  while(fgets(line, sizeof(line), pFile) != NULL) {
    string str = line;
    // Ignore comments
    str = str.substr(0, str.find(comment));
    
    // Parse the line if it contains a delimiter
    pos delimPos = str.find(delimiter);
    if(delimPos < string::npos)
    {
      // Extract the key
      string key = str.substr(0, delimPos);
      str.replace(0, delimPos+skip, "");
      trimInfo(key);
      trimInfo(str);
      mapConf[key] = str; // Store key and value
    }
  }
  fclose (pFile);
  
  DEBUG_LOGS = (mapConf.find("DEBUG_LOGS") != mapConf.end()) ? (mapConf["DEBUG_LOGS"] == "true") ? true : false : false;
  DEBUG_REQUESTS = (mapConf.find("DEBUG_REQUESTS") != mapConf.end()) ? (mapConf["DEBUG_REQUESTS"] == "true") ? true : false : false;
  DEBUG_APP_OTHERS = (mapConf.find("DEBUG_APP_OTHERS") != mapConf.end()) ? (mapConf["DEBUG_APP_OTHERS"] == "true") ? true : false : false;
  
  DB_PATH   = (mapConf.find("DB_PATH") != mapConf.end()) ? mapConf["DB_PATH"] : "/data/nessDB";
  DB_BUFFER = (1024*1024*2);
  FILTER_PATH = (mapConf.find("FILTER_PATH") != mapConf.end()) ? mapConf["FILTER_PATH"] : ".";
  FILTER_SSL = (mapConf.find("FILTER_SSL") != mapConf.end()) ? mapConf["FILTER_SSL"] : "access.log";
  FILTER_URL1 = (mapConf.find("FILTER_URL1") != mapConf.end()) ? mapConf["FILTER_URL1"] : "-event.do";
  FILTER_URL2 = (mapConf.find("FILTER_URL2") != mapConf.end()) ? mapConf["FILTER_URL2"] : ".do";
  EXCLUDE_MOD = (mapConf.find("EXCLUDE_MOD") != mapConf.end()) ? mapConf["EXCLUDE_MOD"] : "_v0";
  
  COMPRESSION = (mapConf.find("COMPRESSION") != mapConf.end()) ? (mapConf["COMPRESSION"] == "on") ? true : false : false;
  LISTENING_PORT = (mapConf.find("LISTENING_PORT") != mapConf.end()) ? mapConf["LISTENING_PORT"] : "9999";
  LOG_FILE_FORMAT = (mapConf.find("LOG_FILE_FORMAT") != mapConf.end()) ? mapConf["LOG_FILE_FORMAT"] : "timestamp";
  LOG_FILE_PATH = (mapConf.find("LOG_FILE_PATH") != mapConf.end()) ? mapConf["LOG_FILE_PATH"] : "myFile.txt";
  
  int val = 10;
  if (mapConf.find("LOGS_READ_INTERVAL") != mapConf.end()) {
    sscanf(mapConf["LOGS_READ_INTERVAL"].c_str(), "%d", &val);
  }
  LOGS_READ_INTERVAL = val;
}


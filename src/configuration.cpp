/*!
 * \file configuration.cpp
 * \brief Configuration reader for mooWApp
 * \author Xavier ETCHEBER
 */

#include <iostream>
#include <string>
#include <map> // Conf info
#include <stdio.h> // fopen, fgets, fclose, sscanf

// Boost
#include <boost/algorithm/string.hpp> // Split
#include <boost/lexical_cast.hpp> // lexical_cast

#include "global.h"
#include "configuration.h"

using namespace std;

void Config::trimInfo(string& s) {
  static const char whitespace[] = " \n\t\v\r\f";
  s.erase( 0, s.find_first_not_of(whitespace) );
  s.erase( s.find_last_not_of(whitespace) + 1U );
}

Config::Config(string cfgFile) {
  typedef string::size_type pos;
  const string delimiter = "=", comment = "#", separator = "|";
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
  
  DB_PATH   = (mapConf.find("DB_PATH") != mapConf.end()) ? mapConf["DB_PATH"] : "/data/";
  DB_NAME   = (mapConf.find("DB_NAME") != mapConf.end()) ? mapConf["DB_NAME"] : "storage.db";
  FILTER_PATH = (mapConf.find("FILTER_PATH") != mapConf.end()) ? mapConf["FILTER_PATH"] : ".";
  FILTER_SSL = (mapConf.find("FILTER_SSL") != mapConf.end()) ? mapConf["FILTER_SSL"] : "access.log";
  
  string strPageGroups = (mapConf.find("FILTER_EXTENSION") != mapConf.end()) ? mapConf["FILTER_EXTENSION"] : "w";
  set<string> setPageGroups, setExtensions;
  boost::split(setPageGroups, strPageGroups, boost::is_any_of(separator));
  set<string>::iterator it;
  for(it=setPageGroups.begin(); it!=setPageGroups.end(); it++) {
    if (mapConf.find(*it) != mapConf.end()) {
      setExtensions.clear();
      boost::split(setExtensions, mapConf[*it], boost::is_any_of(separator));
      FILTER_EXTENSION.insert( pair<string, set<string> >(*it, setExtensions));
    } else {
      cerr << "Missing configuration for key=" << *it << endl; 
      throw;
    }
  }
  
  FILTER_URL1 = (mapConf.find("FILTER_URL1") != mapConf.end()) ? mapConf["FILTER_URL1"] : " 200 ";
  FILTER_URL2 = (mapConf.find("FILTER_URL2") != mapConf.end()) ? mapConf["FILTER_URL2"] : " 302 ";
  FILTER_URL3 = (mapConf.find("FILTER_URL3") != mapConf.end()) ? mapConf["FILTER_URL3"] : " 404 ";
  EXCLUDE_MOD = (mapConf.find("EXCLUDE_MOD") != mapConf.end()) ? mapConf["EXCLUDE_MOD"] : "_v0";
  
  COMPRESSION = (mapConf.find("COMPRESSION") != mapConf.end()) ? (mapConf["COMPRESSION"] == "on") ? true : false : false;
  LISTENING_PORT = (mapConf.find("LISTENING_PORT") != mapConf.end()) ? mapConf["LISTENING_PORT"] : "9999";
  
  unsigned short logFileNb = 1;
  if (mapConf.find("LOGS_FILES_NB") != mapConf.end()) {
    sscanf(mapConf["LOGS_FILES_NB"].c_str(), "%hd", &logFileNb);
    if (logFileNb < 1) logFileNb = 1;
  }
  LOGS_FILES_NB = logFileNb;
  for (unsigned short i = 1; i <= logFileNb; i++) {
    string strI = boost::lexical_cast<std::string>(i);
    LOGS_FILES_CONFIG.insert( make_pair(i,
      make_pair(
        (mapConf.find("LOG_FILE_FORMAT."+strI) != mapConf.end()) ? mapConf["LOG_FILE_FORMAT."+strI] : "timestamp" ,
        (mapConf.find("LOG_FILE_PATH."+strI) != mapConf.end()) ? mapConf["LOG_FILE_PATH."+strI] : "myFile.log."+strI
      )
    ));
  }
  
  int val = 10;
  if (mapConf.find("LOGS_READ_INTERVAL") != mapConf.end()) {
    sscanf(mapConf["LOGS_READ_INTERVAL"].c_str(), "%d", &val);
  }
  LOGS_READ_INTERVAL = val;
}

Config Config::singleton;
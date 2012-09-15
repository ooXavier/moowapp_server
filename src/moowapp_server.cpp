/*!
 * \file moowapp_server.cpp
 * \brief Web Statistics DB Server aka : mooWApp
 * \author Xavier ETCHEBER
 * \version 0.2.3
 */

#include <iostream>
#include <string>
#include <sstream> // stringstrezm
#include <fstream> // ifstream, ofstream
#include <set>
#include <vector> // Line log analyse
#include <signal.h> // Handler for Ctrl+C

// Boost
#include <boost/progress.hpp> // Timing system
#include <boost/thread/thread.hpp> // Thread system
#include <boost/algorithm/string.hpp> // split
#include <boost/date_time/posix_time/posix_time.hpp> // Conversion date
#include <boost/date_time/gregorian/parsers.hpp> // Parser date
#include <boost/spirit/include/karma.hpp> // int to string
#include <boost/interprocess/sync/scoped_lock.hpp> // Lock for mutex
#include <boost/interprocess/sync/named_mutex.hpp> // Mutex

// mooWApp
#include "global.h"
#include "configuration.h"
#include "db_access_berkeleydb.h" //-1.8.1
#include "log_reader.h"

// mongoose web server
#include "mongoose.h"

using namespace std;
Db *db = NULL;          //!< DB pointer
Config c;               //!< Read configuration file
boost::mutex mutex;     //!< Mutex for thread blocking
struct mg_context *ctx; //!< Pointer of request's context 
bool quit;              //!< Boolean used to quit server properly

/*!
 * \fn int getDBModules(set<string> &setModules)
 * \brief Return a set of web modules stored in DB.
 *
 * \param[in, out] setModules A set to store the web modules names.
 * \param[in] modulesLine Key in DB to look for modules.
 */
int getDBModules(set<string> &setModules, const string modulesLine) {
  //-- Create a set of all known applications in DB
  string strModules = dbw_get(db, modulesLine);
  
  if (strModules.length() <= 0)
    return 1;
  
  boost::split(setModules, strModules, boost::is_any_of("/"));
  setModules.erase(""); // Delete empty module
  
  // Exclude modules configuration
  if (c.EXCLUDE_MOD != "") {
    set<string>::iterator it = setModules.begin();
    while(it!=setModules.end()) {
      if ((*it).find(c.EXCLUDE_MOD, 0) != string::npos) {
        setModules.erase(it++);
      } else {
        ++it;
      }
    }
  }
  
  if (c.DEBUG_APP_OTHERS || c.DEBUG_LOGS) cout << endl << "Known modules (" << setModules.size() << ") for KEY=" << modulesLine << " are :" << strModules << endl;
  return 0;
}

/*!
 * \fn int removeDBModules(set<string> &setDeleteModules)
 * \brief Remove some modules from DB storage.
 *
 * \param[in, out] setDeleteModules A set of modules to be deleted from DB storage.
 */
int removeDBModules(set<string> &setDeleteModules) {
  //-- Create a set of all known applications in DB
  string strModules = dbw_get(db, KEY_MODULES);
  
  if (strModules.length() <= 0)
    return 1;
  
  set<string> setModules;
  boost::split(setModules, strModules, boost::is_any_of("/"));
  setModules.erase(""); // Delete empty module
  
  // Remove "modules to be deleted" from modules set
  set<string>::iterator it, itt;
  for(it=setDeleteModules.begin(); it!=setDeleteModules.end(); it++) {
    for(itt=setModules.begin(); itt!=setModules.end(); itt++) {
      if ((*itt).find((*it), 0) != string::npos) {
        setModules.erase(*itt);
      }
    }
  }
  
  // Convert modules set to DB line
  string value = "";
  for(itt=setModules.begin(); itt!=setModules.end(); itt++) {
    value += (*itt) + "/";
  }
  dbw_remove(db, KEY_MODULES);
  dbw_add(db, KEY_MODULES, value);
  //cout << "New line: " << value << endl;
  
  // Replace in DB
  return 0;
}

/*!
 * \fn void statsAddSumRow(vector< pair<string, map<int, int> > > &vRes, int nbResWithOffset, int offset)
 * \brief Insert in front of the vector in param, a SUM of visits by days
 *
 * \param[in, out] vRes The vector where the SUM will be added.
 * \param[in] nbResWithOffset A number of days
 * \param[in] offset A number of stating day for the answer
 */
void statsAddSumRow(vector< pair<string, map<int, int> > > &vRes, int nbResWithOffset, int offset) {
  map<int, int>::iterator itMap;
  
  if (vRes.size() > 1) { // If only one module, sum is useless
    map<int, int> mapSum;
      
    for (int i=offset; i < nbResWithOffset; i++) {
      mapSum.insert( pair<int,int>(i, 0) );
    }  
    for (int maxI=vRes.size(), i=0; i<maxI; i++) {
      for (itMap=(vRes[i].second).begin(); itMap != (vRes[i].second).end(); itMap++) {
        mapSum[itMap->first] += itMap->second;
      }
    }
    vRes.insert(vRes.begin(), make_pair("All", mapSum));
  }
}

/*!
 * \fn void statsConstructResponse(vector< pair<string, map<int, int> > > &vRes, string &response)
 * \brief Return a JSON part string of the content of a vector of stats.
 *
 * \param[in, out] vRes The vector of all stats grouped by module and stored by day.
 * \param[in, out] response Response string in JSON format (only part of JSON answer will be returned). Should by empty at call.
 */
void statsConstructResponse(vector< pair<string, map<int, int> > > &vRes, string &response) {
  map<int, int>::iterator itMap;
  for (int maxI=vRes.size(), i=0; i<maxI; i++) {
    // Print web module name or application name for mode "all"
    response += "[\"" + vRes[i].first + "\",{";
    itMap = (vRes[i].second).begin();
    for (int j=0, maxJ=(vRes[i].second).size(); itMap != (vRes[i].second).end(); j++, itMap++) {
      response += "\"" + boost::lexical_cast<string>(itMap->first)
               + "\":" + boost::lexical_cast<string>(itMap->second);
      if (j != (maxJ - 1)) response += ",";
    }
    response += "}]";
    if (i != (maxI - 1)) response += ",";
  }
}

/*!
 * \fn void convertDate(string strDate, string format)
 * \brief Convert a string date from timestamp format to an other format (such as Y-M-D...).
 *
 * \param[in] strDate Date to extract as string. Ex: 1314253853 or Thursday 25 November
 * \param[in] format New format for date.
 */
string convertDate(string strDate, string format) {
  istringstream ss;
  time_t tStamp;
  struct tm * timeinfo;
  string ret;
  char strDateFormated[31];
  
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200
  timeinfo = localtime(&tStamp);
  strftime(strDateFormated, 31, format.c_str(), timeinfo);
  return strDateFormated;
}

/*!
 * \fn void get_qsvar(const struct mg_request_info *ri, const char *name, char *dst, size_t dst_len)
 * \brief Get a value of particular form variable.
 *
 * \param[in] request_info Information about HTTP request.
 * \param[in] name Variable name to decode from the buffer.
 * \param[in] dst Destination buffer for the decoded variable.
 * \param[in] dst_len Length of the destination buffer.
 */
void get_qsvar(const struct mg_request_info *ri, const char *name, char *dst, size_t dst_len) {
  const char *qs = ri->query_string;
  mg_get_var(qs, strlen(qs == NULL ? "" : qs), name, dst, dst_len);
}

/*!
 * \fn void get_request_params(struct mg_connection *conn, const struct mg_request_info *ri, map<string, string> &mapParams)
 * \brief Put all request parameters into a map.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] ri Information about HTTP request.
 * \param[in, out] mapParams Length of the destination buffer.
 */
void get_request_params(struct mg_connection *conn, const struct mg_request_info *ri, map<string, string> &mapParams) {
  char *buf;
  size_t buf_len;
  const char *cl;
  buf_len = 0;
  buf = NULL;
  cl = mg_get_header(conn, "Content-Length");
  if ((!strcmp(ri->request_method, "POST") || !strcmp(ri->request_method, "PUT")) && cl != NULL) {
   buf_len = atoi(cl);
   buf = (char*) malloc(buf_len);
   /// Read in two pieces, to test continuation
   if (buf_len > 2) {
     mg_read(conn, buf, 2);
     mg_read(conn, buf + 2, buf_len - 2);
   } else {
     mg_read(conn, buf, buf_len);
   }
  } else if (ri->query_string != NULL) {
   buf_len = strlen(ri->query_string);
   buf = (char*) malloc(buf_len + 1);
   strcpy(buf, ri->query_string);
  }
  
  string strBuf = string(buf, buf_len);
  //cout << "Buffer value: " << strBuf << endl;
  /// Split sequences separated by & ; Ex var1=val1&var2=val2
  vector<string> vectStrAnd;
  boost::split(vectStrAnd, strBuf, boost::is_any_of("&"));
  for(vector<string>::iterator tok_iter = vectStrAnd.begin(); tok_iter != vectStrAnd.end(); ++tok_iter) {
    size_t found = (*tok_iter).find("=");
    if (found != string::npos) {
      /// Split sequences separated by = ; Ex: var1=val1
      vector<string> vectStrEqual;
      boost::split(vectStrEqual, *tok_iter, boost::is_any_of("="));
      //cout << "Value for [" << vectStrEqual[0] << "] is [" << vectStrEqual[1] << "]" << endl;
      mapParams.insert(pair<string, string>(vectStrEqual[0], vectStrEqual[1]));
    }
  }
  free(buf);
}

/*!
 * \fn void filteringPeriod(struct mg_connection *conn, const struct mg_request_info *ri, int i, string &strYearMonth, set<string> &setDateToKeep, map<string, string> &mapParams)
 * \brief Return a set of web modules stored in DB.
 *
 * \param[in] conn A set to store the web modules names.
 * \param[in] ri A set to store the web modules names.
 * \param[in] i A set to store the web modules names.
 * \param[in, out] strYearMonth A set to store the web modules names.
 * \param[in, out] setDateToKeep A set to store the web modules names.
 * \param[in, out] mapParams A set to store the web modules names.
 */
void filteringPeriod(struct mg_connection *conn, const struct mg_request_info *ri, int i, string &strYearMonth, set<string> &setDateToKeep, map<string, string> &mapParams) {
  string strAppDays; // Days in the month, starting at 0. Ex: 0-30 or 0-2,4,6-30
  ostringstream oss;
  map<string, string>::iterator itParam;
  
  /// Get periods for that project (filtering)
  oss << "p_" << i << "_d";
  if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
    strAppDays = itParam->second;
  } else {
    strAppDays = "1-31"; // Default value : complete month
  }
  oss.str("");
  //if (c.DEBUG_REQUESTS) cout << " days=" << strAppDays;

  /// Store only date to be returned
  if (strAppDays != "1-31") {
  
    /// Split sequences separated by coma ; Ex 1-4,6,8-12,15
    vector<string> vectStrComa;
    boost::split(vectStrComa, strAppDays, boost::is_any_of(","));
    for(vector<string>::iterator tok_iter = vectStrComa.begin(); tok_iter != vectStrComa.end(); ++tok_iter) {
    
      size_t found = (*tok_iter).find("-");
      if (found != string::npos) {
        /// Split sequences separated by - ; Ex: 3-30
        vector<string> vectStrDash;
        boost::split(vectStrDash, *tok_iter, boost::is_any_of("-"));
        vector<string>::iterator ttok_iter = vectStrDash.begin();
        int start, end;
        try {
          start = boost::lexical_cast<int>(*ttok_iter);
          if(ttok_iter != vectStrDash.end()) {
            ++ttok_iter;
          }
          end = boost::lexical_cast<int>(*ttok_iter);
          for(;start <= end; ++start) {
            oss << strYearMonth << std::setw(2) << std::setfill('0') << start;
            //if (c.DEBUG_REQUESTS) cout <<  " Added: " << oss.str();
            setDateToKeep.insert(oss.str());
            oss.str("");
          }
        } catch(boost::bad_lexical_cast &) {}
      
      } else {
        // Single numbers : Ex: 4,6,8
        oss << strYearMonth << std::setw(2) << std::setfill('0') << *tok_iter;
        //if (c.DEBUG_REQUESTS) cout <<  " Added: " << oss.str();
        setDateToKeep.insert(oss.str());
        oss.str("");
      }
    }
  }
}

/*!
 * \fn bool handle_jsonp(struct mg_connection *conn, const struct mg_request_info *request_info)
 * \brief Tell if the request is a JSON call
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \return true if "callback" param is present in query string, false otherwise.
 */
bool handle_jsonp(struct mg_connection *conn, const struct mg_request_info *request_info) {
  char cb[64];
  get_qsvar(request_info, "callback", cb, sizeof(cb));
  if (cb[0] != '\0') {
    mg_printf(conn, "%s(", cb);
  }
  return cb[0] == '\0' ? false : true;
}

/*!
 * \fn void stats_app_intra(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_intra context.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_app_intra
 */
void stats_app_intra(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, offset;
  unsigned int iVisit, minVisit;
  ostringstream oss;
  string strMode;        // Mode. Ex: app or all
  string strModules;     // Number of modules. Ex: 4
  string strDates;       // Number of dates. Ex: 60
  string strType;        // Mode. Ex: 1:visits, 2:views, 3:statics
  string strOffset;      // Date offset. Ex: 60
  string strApplication; // Application name. Ex: Calendar
  string strDate;        // Start date. Ex: 1314253853 or Thursday 25 November
  string strModule;      // Modules name. Ex: gerer_connaissance
  string visit;
  map<int, string> mapDate;
  map<int, string>::iterator itm;
  set<string> setModules, setOtherModules;
  set<string>::iterator its;
  
  /// Get parameters in request.
  map<string, string> mapParams;
  map<string, string>::iterator itParam;
  get_request_params(conn, ri, mapParams);
  
  /// Check parameters values
  if ((itParam = mapParams.find("mode")) != mapParams.end()) {
    strMode = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: mode");
    return;
  }
  if (strMode == "all") {
    if ((itParam = mapParams.find("apps")) != mapParams.end()) {
      strModules = itParam->second;
      getDBModules(setOtherModules, KEY_MODULES);
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: apps");
      return;
    }
  } else {
    if ((itParam = mapParams.find("modules")) != mapParams.end()) {
      strModules = itParam->second;
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: modules");
      return;
    }
  }
  if ((itParam = mapParams.find("dates")) != mapParams.end()) {
    strDates = itParam->second;
    sscanf(strDates.c_str(), "%d", &max);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: dates");
    return;
  }
  if ((itParam = mapParams.find("offset")) != mapParams.end()) {
    strOffset = itParam->second;
    sscanf(strOffset.c_str(), "%d", &offset);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: offset");
    return;
  }
  if ((itParam = mapParams.find("type")) != mapParams.end()) {
    strType = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: type");
    return;
  }
  
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  /// Set each date to according offset in response.
  max += offset;
  int ii = 0, iii = 0;
  for(i = offset; i < max; i++, ii++) {
    if (ii!=0 && ii%6 == 0) { ii = 0; iii+=10; }
    oss << "d_" << (offset + ii + iii);
    if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
      strDate = itParam->second;
      //cout << "ici: i=" << i << " ii=" << ii << " iii=" << iii << " => " << (offset + ii + iii) << " strDate=" << strDate << endl;
      mg_printf(conn, "\"%d\":\"%s\",", (offset + ii + iii), strDate.c_str()); // Timestamp returned
      
      mapDate.insert( pair<int,string>((offset + ii + iii), convertDate(strDate, "%Y-%m-%d") ) ); // Convert timestamp to Y-m-d
    }  
    oss.str("");
  }
  
  /// Set Mode and Date in response.
  // Extract "Day NDay Month" from last timestamp
  // Put mode and label after the last date
  i = offset + ii + iii;
  mg_printf(conn, "\"%d\":\"intra\",\"%d\":\"%s\"},", i, i+1, convertDate(strDate, "%A %d %B").c_str());
  
  /// Build visits stats in response for each modules.
  vector< pair<string, map<int, int> > > vRes;
  nbApps = 0;
  sscanf(strModules.c_str(), "%d", &nbApps);
  if (c.DEBUG_REQUESTS) cout << "stats_app_intra - with " << nbApps << flush;
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (strMode == "all") {
      oss << "p_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strApplication = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS && i==0) cout << " apps." << endl;

      // Get nb module of that app in request
      nbModules = 0;
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        sscanf((itParam->second).c_str(), "%d", &nbModules);
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << " with " << strModules << " modules in app " << strApplication << " [" << flush;

      // Loop to put modules from request in a set
      setModules.clear();
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
          strModule = itParam->second;
        } else {
          continue;
        }
        oss.str("");
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);

        // Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;
      
      //-- and each module in an app
      for(itm=mapDate.begin(); itm!=mapDate.end(); itm++) {
        //-- Get nb visit from DB
        for(its=setModules.begin(), minVisit=0; its!=setModules.end(); its++) {
          // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
          oss << *its << '/' << strType << "/" << (*itm).second << '/' << (*itm).first;
          // Search Key (oss) in DB
          visit = dbw_get(db, oss.str());
          if (visit.length() > 0) {
            // Update nb visit of the app for this day
            iVisit = 0;
            sscanf(visit.c_str(), "%d", &iVisit);
            minVisit += iVisit;
          }
          // Return last nb visit
          //if (c.DEBUG_REQUESTS && strcmp("xxx", strApplication)==0) cout << " visits for " << oss.str() << " (" << (*itm).first << ")= " << boost::lexical_cast<int>(visit) << endl; 
          oss.str("");
        }  
        mapResMod.insert(pair<int, int>((*itm).first, minVisit));
      }
    
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      if (c.DEBUG_REQUESTS && i==0) cout << " module(s) in app." << endl;
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strModule = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << "- module=" << strModule << endl;
      
      //-- and each dates.
      for(itm=mapDate.begin(); itm!=mapDate.end(); itm++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
        oss << strModule << '/' << strType << "/" << (*itm).second << '/' << (*itm).first;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        int iVisit = 0;
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
          sscanf(visit.c_str(), "%d", &iVisit);
        }
        // Return nb visit
        //if (c.DEBUG_REQUESTS) cout << " visits for " << oss.str() << " (" << (*itm).first << ")= " << boost::lexical_cast<int>(visit) << endl; 
        mapResMod.insert(pair<int, int>((*itm).first, iVisit));
      }
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  /// In all mode, add an "Others" application
  if (strMode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    //-- Get nb visit from DB
    for(itm=mapDate.begin(); itm!=mapDate.end(); itm++) {
      //-- Get nb visit from DB
      for(its=setOtherModules.begin(), minVisit=0; its!=setOtherModules.end(); its++) {
        if (c.DEBUG_APP_OTHERS && itm == mapDate.begin()) cout << *its << ", ";
        // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
        oss << *its << '/' << strType << "/" << (*itm).second << '/' << (*itm).first;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
          iVisit = 0;
          sscanf(visit.c_str(), "%d", &iVisit);
          minVisit += iVisit;
        }
        // Return last nb visit
        //if (c.DEBUG_REQUESTS && strcmp("xxx", strApplication)==0) cout << " visits for " << oss.str() << " (" << (*itm).first << ")= " << boost::lexical_cast<int>(visit) << endl; 
        oss.str("");
      }
      if (c.DEBUG_APP_OTHERS && itm == mapDate.begin()) cout << endl;
      mapResMod.insert(pair<int, int>((*itm).first, minVisit));
    }
    vRes.push_back(make_pair("Others", mapResMod));
  }
  
  /// Add a SUM row serie
  statsAddSumRow(vRes, (max-offset), offset); // 36 a day without offset
  
  //-- Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  /// Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
  
  //In needed, below is a mocked response
  /*mg_printf(conn, "%s", "[{\"60\":1313726400,\"61\":1313727000,\"62\":1313727600,\"63\":1313728200,\"64\":1313728800,\"65\":1313729400,\"66\":1313730000,\"67\":1313730600,\"68\":1313731200,\"69\":1313731800,\"70\":1313732400,\"71\":1313733000,\"72\":1313733600,\"73\":1313734200,\"74\":1313734800,\"75\":1313735400,\"76\":1313736000,\"77\":1313736600,\"78\":1313737200,\"79\":1313737800,\"80\":1313738400,\"81\":1313739000,\"82\":1313739600,\"83\":1313740200,\"84\":1313740800,\"85\":1313741400,\"86\":1313742000,\"87\":1313742600,\"88\":1313743200,\"89\":1313743800,\"90\":1313744400,\"91\":1313745000,\"92\":1313745600,\"93\":1313746200,\"94\":1313746800,\"95\":1313747400,\"96\":\"intra\",\"97\":\"Vendredi 19 août 2011\"},");
  mg_printf(conn, "%s", "[\"Connaissance Client\",{\"60\":0,\"61\":0,\"62\":144,\"63\":455,\"64\":234,\"65\":456,\"66\":0,\"67\":0,\"68\":0,\"69\":0,\"70\":0,\"71\":0,\"72\":0,\"73\":0,\"74\":0,\"75\":0,\"76\":0,\"77\":0,\"78\":0,\"79\":0,\"80\":0,\"81\":0,\"82\":0,\"83\":0,\"84\":0,\"85\":0,\"86\":0,\"87\":0,\"88\":0,\"89\":0,\"90\":0,\"91\":0,\"92\":0,\"93\":0,\"94\":0,\"95\":0}],");
  mg_printf(conn, "%s", "[\"agenda_application\",{\"60\":0,\"61\":0,\"62\":0,\"63\":0,\"64\":0,\"65\":0,\"66\":0,\"67\":0,\"68\":0,\"69\":0,\"70\":0,\"71\":0,\"72\":0,\"73\":0,\"74\":0,\"75\":0,\"76\":0,\"77\":0,\"78\":0,\"79\":0,\"80\":0,\"81\":0,\"82\":0,\"83\":0,\"84\":0,\"85\":0,\"86\":0,\"87\":0,\"88\":0,\"89\":0,\"90\":0,\"91\":0,\"92\":0,\"93\":0,\"94\":0,\"95\":0}],");
  mg_printf(conn, "%s", "[\"prise_rdv\",{\"60\":0,\"61\":0,\"62\":0,\"63\":0,\"64\":0,\"65\":0,\"66\":0,\"67\":0,\"68\":0,\"69\":0,\"70\":0,\"71\":0,\"72\":0,\"73\":0,\"74\":0,\"75\":0,\"76\":0,\"77\":0,\"78\":0,\"79\":0,\"80\":0,\"81\":0,\"82\":0,\"83\":0,\"84\":0,\"85\":0,\"86\":0,\"87\":0,\"88\":0,\"89\":0,\"90\":0,\"91\":0,\"92\":0,\"93\":0,\"94\":0,\"95\":0}],");
  mg_printf(conn, "%s", "[\"room_booking\",{\"60\":0,\"61\":0,\"62\":0,\"63\":0,\"64\":0,\"65\":0,\"66\":0,\"67\":0,\"68\":0,\"69\":0,\"70\":0,\"71\":0,\"72\":0,\"73\":0,\"74\":0,\"75\":0,\"76\":0,\"77\":0,\"78\":0,\"79\":0,\"80\":0,\"81\":0,\"82\":0,\"83\":0,\"84\":0,\"85\":0,\"86\":0,\"87\":0,\"88\":0,\"89\":0,\"90\":0,\"91\":0,\"92\":0,\"93\":0,\"94\":0,\"95\":0}]]");  
  */
}

/*!
 * \fn void stats_app_day(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_day context.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_app_day
 */
void stats_app_day(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, k, iVisit;
  unsigned int hourVisit;
  string strDates;       // Number of dates. Ex: 60
  string strDate;        // Start date. Ex: 1314253853 or Thursday 25 November
  string strModules;     // Number of modules. Ex: 4
  string strApplication; // Application name. Ex: Calendar
  string strModule;      // Modules name. Ex: module_test_1
  string strMode;        // Mode. Ex: app or all
  string strType;        // Mode. Ex: 1:visits, 2:views, 3:statics
  ostringstream oss;
  string visit;
  set<string> setModules, setOtherModules;
  set<string>::iterator it;
  unsigned int nbVisitForApp;
  
  /// Get parameters in request.
  map<string, string> mapParams;
  map<string, string>::iterator itParam;
  get_request_params(conn, ri, mapParams);
  
  /// Check parameters values
  if ((itParam = mapParams.find("mode")) != mapParams.end()) {
    strMode = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: mode");
    return;
  }
  if (strMode == "all") {
    if ((itParam = mapParams.find("apps")) != mapParams.end()) {
      strModules = itParam->second;
      getDBModules(setOtherModules, KEY_MODULES);
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: apps");
      return;
    }
  } else {
    if ((itParam = mapParams.find("modules")) != mapParams.end()) {
      strModules = itParam->second;
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: modules");
      return;
    }
  }
  if ((itParam = mapParams.find("dates")) != mapParams.end()) {
    strDates = itParam->second;
    sscanf(strDates.c_str(), "%d", &max);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: dates");
    return;
  }
  if ((itParam = mapParams.find("type")) != mapParams.end()) {
    strType = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: type");
    return;
  }
  
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  i = 0;
  for(max += i; i < max; i++) {
    oss << "d_" << i;
    if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
      strDate = itParam->second;
      //-- Set each date to according offset in response.
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate.c_str());
    }
    oss.str("");
  }
  
  /// Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  mg_printf(conn, "\"%d\":\"day\",\"%d\":\"%s\"},", i, i+1, convertDate(strDate, "%A %d %B").c_str() );
  
  /// Convert timestamp to Y-m-d
  string strDateFormated = convertDate(strDate, "%Y-%m-%d");
  
  /// Build visits stats in response for each modules or app.
  vector< pair<string, map<int, int> > > vRes;
  nbApps = 0;
  sscanf(strModules.c_str(), "%d", &nbApps);
  if (c.DEBUG_REQUESTS) cout << "stats_app_day - with " << nbApps << flush;
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (strMode == "all") {
      oss << "p_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strApplication = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS && i==0) cout << " apps." << endl;

      /// Get nb module of that app in request
      nbModules = 0;
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        sscanf((itParam->second).c_str(), "%d", &nbModules);
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << " with " << nbModules << " modules in app " << strApplication << " [" << flush;

      /// Loop to put modules from request in a set
      setModules.clear();
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
          strModule = itParam->second;
        } else {
          continue;
        }
        oss.str("");
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);

        /// Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;

      /// and each module in an app
      hourVisit = 0;
      for(int l=k=0, max=DB_TIMES_SIZE;l<max;l++) {
        //-- Get nb visit from DB
        for(it=setModules.begin(); it!=setModules.end(); it++) {
          // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
          oss << *it << '/' << strType << "/" << strDateFormated << '/' << dbTimes[l];
          // Search Key (oss) in DB
          visit = dbw_get(db, oss.str());
          iVisit = 0;
          if (visit.length() > 0) {
            sscanf(visit.c_str(), "%d", &iVisit);
            //if (*it == "bureau") cout << oss.str() << " = " << iVisit << endl;
          }
          int timeVal = 0;
          sscanf(dbTimes[l].c_str(), "%d", &timeVal);
          if (floor(timeVal/10) == k) {
            hourVisit += iVisit;
          } else {
            if (c.DEBUG_REQUESTS) cout << " visits for " << oss.str() << " k=" << k << " hourVisit=" << hourVisit << " dbTimes[i]=" << dbTimes[l] << endl;
            // Return nb visit
            mapResMod.insert(pair<int, int>(k, hourVisit));
            hourVisit = iVisit;
            k = floor(timeVal/10);
          }
          oss.str("");
        }
      }
      //cout << "LAST k=" << k << " hourVisit=" << hourVisit << endl;
      /// Return last nb visit
      mapResMod.insert(pair<int, int>(23, hourVisit));
    
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      if (c.DEBUG_REQUESTS && i==0) cout << " modules in app." << endl;
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strModule = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << " - module=" << strModule;
    
      /// Get nb visit from DB
      k = hourVisit = 0;
      for(int l=0, max=DB_TIMES_SIZE;l<max;l++) {
        // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
        oss << strModule << '/' << strType << "/" << strDateFormated << '/' << dbTimes[l];
        //if (c.DEBUG_REQUESTS && l==0) cout << " visits for " << oss.str() << endl;
        /// Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        iVisit = 0;
        if (visit.length() > 0) {
          sscanf(visit.c_str(), "%d", &iVisit);
          //if (c.DEBUG_REQUESTS) cout << oss.str() << " = " << iVisit << endl;
        }
        int timeVal = 0;
        sscanf(dbTimes[l].c_str(), "%d", &timeVal);
        if (floor(timeVal/10) == k) {
          hourVisit += iVisit;
          //cout << "hourVisit=" << hourVisit << endl;
        } else {
          if (c.DEBUG_REQUESTS) cout << oss.str() << " => " << hourVisit << endl;
          /// Return nb visit
          mapResMod.insert(pair<int, int>(k, hourVisit));
          hourVisit = iVisit;
          k = floor(timeVal/10);
        }
        oss.str("");
      } 
      //if (c.DEBUG_REQUESTS) cout << "LAST k=" << k << " hourVisit=" << hourVisit << endl;
      if (c.DEBUG_REQUESTS) cout << endl;
      /// Return last nb visit
      mapResMod.insert(pair<int, int>(23, hourVisit));
      
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  /// In all mode, add an "Others" application
  if (strMode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules (" << setOtherModules.size() << "): " << flush;
    
    hourVisit = 0;
    /// Get nb visit from DB
    for(int l = k = 0, max=DB_TIMES_SIZE;l<max;l++) {
      int timeVal = 0;
      sscanf(dbTimes[l].c_str(), "%d", &timeVal);
      for(it=setOtherModules.begin(), nbVisitForApp = 0; it!=setOtherModules.end(); it++) {
        if (c.DEBUG_APP_OTHERS && l == 0) cout << *it << ", ";
        /// Build Key ex: "creer_modifier_retrocession/1/2011-04-24/150";
        oss << *it << '/' << strType << "/" << strDateFormated << '/' << timeVal;
        /// Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        if (visit.length() > 0) {
          iVisit = 0;
          sscanf(visit.c_str(), "%d", &iVisit);
          nbVisitForApp += iVisit;
          //if (c.DEBUG_APP_OTHERS && *it == "bureau") cout << oss.str() << " = " << boost::lexical_cast<int>(visit) << endl;
        }
        oss.str("");
      }
      if (c.DEBUG_APP_OTHERS && l == 0) cout << endl;
      if (floor(timeVal/10) == k) {
        hourVisit += nbVisitForApp;
      } else {
        //if (c.DEBUG_APP_OTHERS) cout << "HERE k=" << k << " dbTimes[i]=" << timeVal << " hourVisit=" << hourVisit << endl;
        /// Return nb visit
        mapResMod.insert(pair<int,int>(k, hourVisit));
        hourVisit = nbVisitForApp;
        k = floor(timeVal/10);
      }
    }
    /// Return last nb visit
    mapResMod.insert(pair<int,int>(23, hourVisit));
    oss.str("");
    vRes.push_back(make_pair("Others", mapResMod));
  }
  
  /// Add a SUM row serie
  statsAddSumRow(vRes, 24, 0); // 24hours a day without offset
  
  /// Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  /// Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn void stats_app_week(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_week context.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_app_week
 */
void stats_app_week(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, offset, iVisit;
  string strDates;   // Number of dates. Ex: 31
  string strDate;    // Start date. Ex: 1314253853 or Thursday 25 November
  string strOffset;  // Date offset. Ex: 11
  string strModules; // Number of modules. Ex: 4
  string strApplication;  // Application name. Ex: Calendar
  string strModule;  // Modules name. Ex: gerer_connaissance
  string strMode;     // Mode. Ex: app or all
  string strType;     // Mode. Ex: 1:visits, 2:views, 3:statics
  ostringstream oss;
  string visit, date;
  set<string> setDate, setModules, setOtherModules;
  set<string>::iterator it, itt;
  unsigned int nbVisitForApp;

  /// Get parameters in request.
  map<string, string> mapParams;
  map<string, string>::iterator itParam;
  get_request_params(conn, ri, mapParams);
  
  /// Check parameters values
  if ((itParam = mapParams.find("mode")) != mapParams.end()) {
    strMode = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: mode");
    return;
  }
  if (strMode == "all") {
    if ((itParam = mapParams.find("apps")) != mapParams.end()) {
      strModules = itParam->second;
      getDBModules(setOtherModules, KEY_MODULES);
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: apps");
      return;
    }
  } else {
    if ((itParam = mapParams.find("modules")) != mapParams.end()) {
      strModules = itParam->second;
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: modules");
      return;
    }
  }
  if ((itParam = mapParams.find("dates")) != mapParams.end()) {
    strDates = itParam->second;
    sscanf(strDates.c_str(), "%d", &max);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: dates");
    return;
  }
  if ((itParam = mapParams.find("offset")) != mapParams.end()) {
    strOffset = itParam->second;
    sscanf(strOffset.c_str(), "%d", &offset);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: offset");
    return;
  }
  if ((itParam = mapParams.find("type")) != mapParams.end()) {
    strType = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: type");
    return;
  }
  
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  /// Create a set for the Dates to loop easily
  max += offset;
  for(i = offset; i < max; i++) {
    oss << "d_" << i;
    if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
      strDate = itParam->second;
      //-- Set each date to according offset in response.
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate.c_str());
      // Convert timestamp to Y-m-d
      try {
        boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
        boost::gregorian::date d = pt.date();
        date = boost::gregorian::to_iso_extended_string(d);
        setDate.insert(date);
      } catch(boost::bad_lexical_cast &) {}
    }
    oss.str("");
  }
  
  /// Save Year-Month for later
  size_t found = date.find_last_of("-");
  string strYearMonth = date.substr(0, found+1);
  
  /// Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp // Depend one request intra, day, week, month, year
  mg_printf(conn, "\"%d\":\"month\",\"%d\":\"%s\"},", i, i+1, convertDate(strDate, "%B %Y").c_str() );
  
  /// Build visits stats in response for each modules.
  vector< pair<string, map<int, int> > > vRes;
  nbApps = 0;
  sscanf(strModules.c_str(), "%d", &nbApps);
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (strMode == "all") {
      oss << "p_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strApplication = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << "stats_app_week - app=" << strApplication;
      
      //-- Filter for days
      set<string> setDateToKeep;
      filteringPeriod(conn, ri, i, strYearMonth, setDateToKeep, mapParams);
      
      // Get nb module of that app in request
      nbModules = 0;
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        sscanf((itParam->second).c_str(), "%d", &nbModules);
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << " with " << nbModules << " modules in app [" << flush;
      
      // Loop to put modules from request in a set
      setModules.clear();
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
          strModule = itParam->second;
        } else {
          continue;
        }
        oss.str("");
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);
        
        // Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;
        
      //-- and loop for each dates.
      sscanf(strOffset.c_str(), "%d", &j);
      for(it=setDate.begin(); it!=setDate.end(); j++) {
        nbVisitForApp = 0;
        // If *it is not in setDateToKeep, return 0 values
        set<string>::iterator itSet = setDateToKeep.find(*it);
        if ((setDateToKeep.size() == 0) || (itSet != setDateToKeep.end())) {
          //-- and each module in an app
          for(itt=setModules.begin(); itt!=setModules.end(); itt++) {
            //-- Get nb visit from DB
            // Build Key ex: "creer_modifier_retrocession/1/2011-04-24";
            oss << *itt << '/' << strType << "/" << *it;
            // Search Key (oss) in DB
            visit = dbw_get(db, oss.str());
            oss.str("");
            if (visit.length() > 0) {
              // Update nb visit of the app for this day
              iVisit = 0;
              sscanf(visit.c_str(), "%d", &iVisit);
              nbVisitForApp += iVisit;
            }
          }
        }
        if (c.DEBUG_REQUESTS) cout << *it << " => " << nbVisitForApp << " visits." << endl;
        it++;
        
        // Return nb visit
        mapResMod.insert(pair<int, int>(j, nbVisitForApp));
      }
      
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strModule = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << "stats_app_week: " << strModule << endl;
    
      //-- and each dates.
      sscanf(strOffset.c_str(), "%d", &j);
      for(it=setDate.begin(); it!=setDate.end(); j++, it++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/2011-04-24";
        oss << strModule << '/' << strType << "/" << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        if (visit.length() == 0) {
          visit = "0";
        }
        //if (c.DEBUG_REQUESTS) cout << "oss:" << oss.str() << " i: " << i << " visit:" << visit << endl;
        oss.str("");
      
        // Return nb visit
        try {
          sscanf(visit.c_str(), "%d", &iVisit);
          mapResMod.insert(pair<int, int>(j, iVisit));
        } catch(boost::bad_lexical_cast &) {}
      }
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  /// In all mode, add an "Others" application
  if (strMode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    sscanf(strOffset.c_str(), "%d", &j);
    for(it=setDate.begin(); it!=setDate.end(); j++, it++) {
      for(itt=setOtherModules.begin(), nbVisitForApp = 0; itt!=setOtherModules.end(); itt++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/2011-04-24";
        oss << *itt << '/' << strType << "/" << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
          iVisit = 0;
          sscanf(visit.c_str(), "%d", &iVisit);
          nbVisitForApp += iVisit;
        }
        
        if (c.DEBUG_APP_OTHERS && it==setDate.begin()) cout << *itt << ", ";
      }
      
      // Return nb visit if != 0
      ///if (nbVisitForApp != 0){
        mapResMod.insert(pair<int,int>(j, nbVisitForApp));
      ///}
    }
    
    vRes.push_back(make_pair("Others", mapResMod));
    
    if (c.DEBUG_APP_OTHERS) cout << endl;
  }
  
  /// Add a SUM row serie
  statsAddSumRow(vRes, max, offset);
  
  /// Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  /// Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
   response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn void stats_app_month(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_month context.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_app_month
 */
void stats_app_month(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, offset, iVisit;
  string strDates;   // Number of dates. Ex: 31
  string strDate;    // Start date. Ex: 1314253853 or Thursday 25 November
  string strOffset;  // Date offset. Ex: 11
  string strModules; // Number of modules. Ex: 4
  string strApplication;  // Application name. Ex: Calendar
  string strModule;  // Modules name. Ex: module_test_1
  string strMode;     // Mode. Ex: app or all
  string strType;     // Mode. Ex: 1 or 2 or 3
  ostringstream oss;
  string mode, visit, date;
  set<string> setDate, setModules, setOtherModules;
  set<string>::iterator it, itt;
  unsigned int nbVisitForApp;
  
  /// Get parameters in request.
  map<string, string> mapParams;
  map<string, string>::iterator itParam;
  get_request_params(conn, ri, mapParams);
  
  /// Check parameters values
  if ((itParam = mapParams.find("mode")) != mapParams.end()) {
    strMode = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: mode");
    return;
  }
  if (strMode == "all") {
    if ((itParam = mapParams.find("apps")) != mapParams.end()) {
      strModules = itParam->second;
      getDBModules(setOtherModules, KEY_MODULES);
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: apps");
      return;
    }
  } else {
    if ((itParam = mapParams.find("modules")) != mapParams.end()) {
      strModules = itParam->second;
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: modules");
      return;
    }
  }
  if ((itParam = mapParams.find("dates")) != mapParams.end()) {
    strDates = itParam->second;
    sscanf(strDates.c_str(), "%d", &max);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: dates");
    return;
  }
  if ((itParam = mapParams.find("offset")) != mapParams.end()) {
    strOffset = itParam->second;
    sscanf(strOffset.c_str(), "%d", &offset);
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: offset");
    return;
  }
  if ((itParam = mapParams.find("type")) != mapParams.end()) {
    strType = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: type");
    return;
  }
  
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  if (c.DEBUG_REQUESTS) cout << "nb=" << strModules << endl;
  
  /// Create a set for the Dates to loop easily
  max += offset;
  for(i = offset; i < max; i++) {
    oss << "d_" << i;
    if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
      strDate = itParam->second;
      //-- Set each date to according offset in response.
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate.c_str());
      // Convert timestamp to Y-m-d
      try {
        boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
        boost::gregorian::date d = pt.date();
        date = boost::gregorian::to_iso_extended_string(d);
        setDate.insert(date);
      } catch(boost::bad_lexical_cast &) {}
    }  
    oss.str("");
  }
  
  /// Save Year-Month for later
  size_t found = date.find_last_of("-");
  string strYearMonth = date.substr(0, found+1);
  
  /// Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp // Depend one request intra, day, week, month, year
  mg_printf(conn, "\"%d\":\"month\",\"%d\":\"%s\"},", i, i+1, convertDate(strDate, "%B %Y").c_str() );
  
  /// Build visits stats in response for each modules or app.
  vector< pair<string, map<int, int> > > vRes;
  nbApps = 0;
  sscanf(strModules.c_str(), "%d", &nbApps);
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (strMode == "all") {
      oss << "p_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strApplication = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << "stats_app_month - app=" << strApplication;
      
      //-- Filter for days
      set<string> setDateToKeep;
      filteringPeriod(conn, ri, i, strYearMonth, setDateToKeep, mapParams);
      
      // Get nb module of that app in request
      nbModules = 0;
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        sscanf((itParam->second).c_str(), "%d", &nbModules);
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << " with " << nbModules << " modules in app [" << flush;
      
      // Loop to put modules from request in a set
      setModules.clear();
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
          strModule = itParam->second;
        } else {
          continue;
        }
        oss.str("");
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);
        
        // Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;
        
      //-- and loop for each dates.
      sscanf(strOffset.c_str(), "%d", &j);
      for(it=setDate.begin(); it!=setDate.end(); j++) {
        nbVisitForApp = 0;
        // If *it is not in setDateToKeep, return 0 values
        set<string>::iterator itSet = setDateToKeep.find(*it);
        if ((setDateToKeep.size() == 0) || (itSet != setDateToKeep.end())) {
          //-- and each module in an app
          for(itt=setModules.begin(); itt!=setModules.end(); itt++) {
            //-- Get nb visit from DB
            // Build Key ex: "creer_modifier_retrocession/1/2011-04-24";
            oss << *itt << '/' << strType << "/" << *it;
            // Search Key (oss) in DB
            visit = dbw_get(db, oss.str());
            oss.str("");
            if (visit.length() > 0) {
              // Update nb visit of the app for this day
              iVisit = 0;
              sscanf(visit.c_str(), "%d", &iVisit);
              nbVisitForApp += iVisit;
            }
          }
        }
        if (c.DEBUG_REQUESTS) cout << *it << " => " << nbVisitForApp << " visits." << endl;
        it++;
        
        // Return nb visit
        mapResMod.insert(pair<int, int>(j, nbVisitForApp));
      }
      
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        strModule = itParam->second;
      } else {
        continue;
      }
      oss.str("");
      if (c.DEBUG_REQUESTS) cout << "stats_app_month - module=" << strModule << endl;
      
      //-- and each dates.
      sscanf(strOffset.c_str(), "%d", &j);
      for(it=setDate.begin(); it!=setDate.end(); j++, it++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/1/2011-04-24";
        oss << strModule << '/' << strType << '/' << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        if (c.DEBUG_REQUESTS) cout << oss.str() << " => j=" << j << " - "<< visit << " visits." << endl;
        oss.str("");
        // Return nb visit if != 0
        if (visit.length() != 0){
          sscanf(visit.c_str(), "%d", &iVisit);
          mapResMod.insert(pair<int,int>(j, iVisit));
        } else {
          mapResMod.insert(pair<int,int>(j, 0));
        }
      }
      
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  /// In all mode, add an "Others" application
  if (strMode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    sscanf(strOffset.c_str(), "%d", &j);
    for(it=setDate.begin(); it!=setDate.end(); j++, it++) {
      for(itt=setOtherModules.begin(), nbVisitForApp = 0; itt!=setOtherModules.end(); itt++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/2011-04-24";
        oss << *itt << '/' << strType << "/" << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
          sscanf(visit.c_str(), "%d", &iVisit);
          nbVisitForApp += iVisit;
        }
        
        if (c.DEBUG_APP_OTHERS && it==setDate.begin()) cout << *itt << ", ";
      }
      
      // Return nb visit
      mapResMod.insert(pair<int,int>(j, nbVisitForApp));
    }
    
    vRes.push_back(make_pair("Others", mapResMod));
    
    if (c.DEBUG_APP_OTHERS) cout << endl;
  }
  
  /// Add a SUM row serie
  statsAddSumRow(vRes, setDate.size(), 0);
  
  /// Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  /// Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn void stats_modules_list(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_modules_list context.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_modules_list
 */
void stats_modules_list(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  int i, nbModules;  // Number of modules. Ex: 4
  string strModule;  // Modules name. Ex: module_test_1
  string strMode;    // Mode. Ex: grouped or all
  ostringstream oss;
  set<string> setModules;
  set<string>::iterator it;

  /// Get parameters in request.
  map<string, string> mapParams;
  map<string,string>::iterator itParam;
  get_request_params(conn, ri, mapParams);
  
  /// Check parameters values
  if ((itParam = mapParams.find("mode")) != mapParams.end()) {
    strMode = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: mode");
    return;
  }
  if (strMode == "all") {
    getDBModules(setModules, KEY_MODULES);
    if (c.DEBUG_REQUESTS) cout << "stats_modules_list - all";
  } else if (strMode == "grouped") {
    if ((itParam = mapParams.find("modules")) != mapParams.end()) {
      sscanf((itParam->second).c_str(), "%d", &nbModules);
    } else {
      mg_printf(conn, "%s", standard_json_reply);
      mg_printf(conn, "%s", "Missing parameter: modules");
      return;
    }
    getDBModules(setModules, KEY_MODULES); // Get all modules
    
    /// Loop to remove modules from request of the set
    for(i = 0; i < nbModules; i++) {
      oss << "m_" << i;
      if ((itParam = mapParams.find(oss.str())) != mapParams.end()) {
        /// Remove this module from the OTHERS list
        setModules.erase(itParam->second);
      } else {
        continue;
      }
      oss.str("");
    }
  }
  
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_write(conn, "[{", 2);
  
  /// Construct response
  if (c.DEBUG_APP_OTHERS) cout << "Others modules (" << setModules.size() << ")." << endl;
  for(it=setModules.begin(), i = 0; it!=setModules.end(); it++, i++) {
    oss << "\"" << i << "\": \"" << *it << "\", ";
  }
  string response = oss.str();
  response = response.substr(0, response.size()-2); // Remove last ", "
  
  /// Set end JSON string in response.
  response += "}]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn void stats_admin_list_mergemodules(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief List modules marked as to be merged in the stats for the next vacation
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_admin_list_mergemodules
 */
void stats_admin_list_mergemodules(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  /// \todo Use me or delete me.
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_write(conn, "[{", 2);
  
  /// Construct response
  string response = "";
  
  /// Set end JSON string in response.
  response += "}]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn void stats_admin_do_mergemodules(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Mark two modules to be merged in the stats for each days collected in the next vacation or do a full delete of a module.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/stats_admin_do_mergemodules?module=webapptodelete&mergein=moduletocountstatsin Merge request
 * \example http://localhost:9999/stats_admin_do_mergemodules?module=webapptodelete&mergein=del Full delete
 */
void stats_admin_do_mergemodules(struct mg_connection *conn, const struct mg_request_info *ri) {
  bool is_jsonp;
  string strModule; // Modules name. Ex: module_test_1
  string moduleMerge;
  
  /// Get parameters in request.
  map<string, string> mapParams;
  map<string,string>::iterator itParam;
  get_request_params(conn, ri, mapParams);
  
  /// Check parameters values
  if ((itParam = mapParams.find("module")) != mapParams.end()) {
    strModule = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: module");
    return;
  }
  if ((itParam = mapParams.find("mergein")) != mapParams.end()) {
    moduleMerge = itParam->second;
  } else {
    mg_printf(conn, "%s", standard_json_reply);
    mg_printf(conn, "%s", "Missing parameter: mergein");
    return;
  }
  
  set<string>::iterator it;
  set<string> setToBeDeleted;
  setToBeDeleted.insert(strModule);
  if (c.DEBUG_LOGS) cout << "Delete: " << strModule << endl;
  removeDBModules(setToBeDeleted);
  
  /// Construct response
  string response = "[{\"delete\": \"" + strModule + "\"";
  
  /// Update list of deleted modules in DB
  set<string> setDeletedModules;
  string strDeletedModules = dbw_get(db, "modules-deleted");
  if (strDeletedModules.length() > 0) {
    boost::split(setDeletedModules, strDeletedModules, boost::is_any_of("/"));
    setDeletedModules.erase(""); // Delete empty module
  }
  for(it=setToBeDeleted.begin(); it!=setToBeDeleted.end(); it++) {
    setDeletedModules.insert(*it);
  }
  
  strDeletedModules = "";
  for(it=setDeletedModules.begin(); it!=setDeletedModules.end(); it++) {
    strDeletedModules += *it + "/";
  }
  if (moduleMerge == "del") {
    dbw_remove(db, KEY_DELETED_MODULES);
    dbw_add(db, KEY_DELETED_MODULES, strDeletedModules);
  } else {
    /// \todo Do use merge
  }
  response +=", \"modules-deleted\": \""+ dbw_get(db, KEY_DELETED_MODULES) +"\"";
  
  /// Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  
  /// Set end JSON string in response.
  response += "}]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn void get_error(struct mg_connection *conn, const struct mg_request_info *request_info)
 * \brief Build an HTTP error response.
 *
 * \param[in] conn Opaque connection handler.
 * \param[in] request_info Information about HTTP request.
 * \example http://localhost:9999/error
 */
void get_error(struct mg_connection *conn, const struct mg_request_info *request_info) {
  mg_printf(conn, "HTTP/1.1 %d XX\r\n"
            "Connection: close\r\n\r\n", request_info->status_code);
  mg_printf(conn, "Error: [%d]", request_info->status_code);
}

/*!
 * \struct uri_config
 * \brief Object for each requests to handle by the web server.
 *
 */
const struct uri_config {
  enum mg_event event;
  const char *uri;
  void (*func)(struct mg_connection *, const struct mg_request_info *);
} uri_config[] = {
  {MG_NEW_REQUEST, "/stats_app_intra", &stats_app_intra},
  {MG_NEW_REQUEST, "/stats_app_day", &stats_app_day},
  {MG_NEW_REQUEST, "/stats_app_week", &stats_app_week},
  {MG_NEW_REQUEST, "/stats_app_month", &stats_app_month},
  {MG_NEW_REQUEST, "/stats_modules_list", &stats_modules_list},
  {MG_NEW_REQUEST, "/stats_admin_do_mergemodules", &stats_admin_do_mergemodules},
  {MG_NEW_REQUEST, "/stats_admin_list_mergemodules", &stats_admin_list_mergemodules},
  {MG_NEW_REQUEST, "/", &get_error},
  {MG_HTTP_ERROR, "", &get_error}
};

/*!
 * \fn void *callback(enum mg_event event, struct mg_connection *conn, const struct mg_request_info *request_info)
 * \brief Call the right function depending on the request context.
 *
 * \param[in] event Which event has been triggered.
 * \param[in] conn Opaque connection handler.
 */
void *callback(enum mg_event event, struct mg_connection *conn) {
  const struct mg_request_info *request_info = mg_get_request_info(conn);
  int i;

  for (i = 0; uri_config[i].uri != NULL; i++) {
    if (event == uri_config[i].event &&
        (event == MG_HTTP_ERROR ||
         !strcmp(request_info->uri, uri_config[i].uri))) {
      uri_config[i].func(conn, request_info);
      return (void*) "processed";
    }
  }
  return NULL;
}

/*!
 * \fn void compressionThread(const Config c)
 * \brief Compress the stats DB at a precise time once a day until 7 day from today.
 *
 * \param[in] c Config object.
 */
void compressionThread(const Config c) {
  uint64_t i, dayVisit;
  int monthNumber, iVisit;
  ostringstream oss;
  string visit;
  string strOss;
  set<string> setModules;
  set<string> setDeletedModules;
  set<string>::iterator it;
  struct tm * timeinfo;
  time_t now;
  char buffer[80];
  
  /// At the first start do a compression from the first day of the year
  boost::gregorian::date dateNow(boost::gregorian::day_clock::universal_day());
  boost::gregorian::date dateLast(dateNow.year()-1, boost::gregorian::Jan, 1);
  
  /// Hold the delay for non compressed stats
  boost::gregorian::date_duration dd_week(c.DAYS_FOR_DETAILS);
  
  /// Specify a fixed time in the day : 03h00 the next day for the next compression of DB
  // FOR DEBUG purpose USE seconds + 10
  boost::posix_time::ptime t = boost::posix_time::second_clock::universal_time() + boost::posix_time::seconds(10);
  //boost::gregorian::date_duration dd(1);
  //boost::posix_time::ptime t(boost::gregorian::day_clock::universal_day() + dd, boost::posix_time::time_duration(3,0,0));
  
  try {
    while(true) {
      boost::gregorian::date today(boost::gregorian::day_clock::universal_day());
      boost::gregorian::date dateToHold(today - dd_week);
      boost::posix_time::ptime timeNow(boost::posix_time::second_clock::universal_time());
      cout << "Obj:" << boost::posix_time::to_simple_string(t) << " & now:" << boost::posix_time::to_simple_string(timeNow) << endl;
	  	
    	/// New iteration check if current time > parsing date fixed (= 03h00)
      /// Compression to file atomically
      if (timeNow >= t && mutex.try_lock()) {
        cout << "----- COMPRESSION RUNNING now (" << boost::posix_time::to_simple_string(timeNow) << ")-----" << endl;
		    
        /// Prepare for the next parsing : add +1 day to date fixed
        // FOR DEBUG
        //t += boost::posix_time::minutes(2*c.LOGS_COMPRESSION_INTERVAL);
        t += boost::posix_time::hours(24);
        
        /// Get current date
        now = time(0);
        timeinfo = localtime(&now);
        strftime (buffer, 80, "%c", timeinfo);
        cout << buffer << endl;
        
        /// Reconstruct list of modules
        setModules.clear();
        getDBModules(setModules, KEY_MODULES);
        
        setDeletedModules.clear();
        getDBModules(setDeletedModules, KEY_DELETED_MODULES);
        
        /// Reloop thru all days since last parsing to j-x in order to remove details and store days only
        boost::gregorian::day_iterator ditr(dateLast);
        for (;ditr <= today; ++ditr) {
          /// produces "C: 2011-Nov-04", "C: 2011-Nov-05", ...
          cout << "C: " << to_simple_string(*ditr) << flush;
          if (ditr <= dateToHold) {
            cout << " R.";
          }
          monthNumber = ditr->month();
          
          /// Check to see if this thread has been interrupted before going into each days of the curent month
          boost::this_thread::interruption_point();
        
          /// Loop thru modules to compress stored stats
          for(it=setModules.begin(); it!=setModules.end(); it++) {
            for(int lineType = 1; lineType <= 2; lineType++) {
              /// lineType=1 -> URL with return code "200"
              /// lineType=2 -> URL with return code "302"
              /// lineType=3 -> URL with return code "404"
              dayVisit = 0;
              oss << *it << '/' << lineType << '/' << ditr->year() << "-" << setfill('0') << setw(2) << monthNumber
                  << "-" << setfill('0') << setw(2) << ditr->day();
              strOss = oss.str();
              for(i=0;i<DB_TIMES_SIZE;i++) {
                // Search Key in DB
                visit = dbw_get(db, strOss+'/'+dbTimes[i]);
                if (visit.length() > 0) {
                  if(c.DEBUG_LOGS && lineType == 1) cout << "C Found: " << strOss << '/' << dbTimes[i] << " =" << visit << "#" << endl;
                  if (ditr <= dateToHold) {
                    /// Delete the current Key in DB
                    dbw_remove(db, strOss+'/'+dbTimes[i]);
                  }
                  /// SUM nb visit from minutes to day
                  sscanf(visit.c_str(), "%d", &iVisit);
                  dayVisit += iVisit;
                }
              }
          
              if (dayVisit > 0) {
                /// Add nb day visits in DB
                if (dbw_add(db, strOss, boost::lexical_cast<string>(dayVisit))) {
                  if(c.DEBUG_LOGS && lineType == 1) cout << "C Added: " << strOss << " = " << dayVisit << endl;
                }
              }
              oss.str("");
            }
          }
          
          /// Loop thru modules to delete to remove stored stats
          for(it=setDeletedModules.begin(); it!=setDeletedModules.end(); it++) {
            for(int lineType = 1; lineType <= 2; lineType++) {
              /// lineType=1 -> URL with return code "200"
              /// lineType=2 -> URL with return code "302"
              /// lineType=3 -> URL with return code "404"
              dayVisit = 0;
              oss << *it << '/' << lineType << '/' << ditr->year() << "-" << setfill('0') << setw(2) << monthNumber
                  << "-" << setfill('0') << setw(2) << ditr->day();
              strOss = oss.str();
              for(i=0;i<DB_TIMES_SIZE;i++) {
                // Search Key in DB
                visit = dbw_get(db, strOss+'/'+dbTimes[i]);
                if (visit.length() > 0) {
                  if (ditr <= dateToHold) {
                    /// Delete the current Key in DB
                    dbw_remove(db, strOss+'/'+dbTimes[i]);
                    if(c.DEBUG_LOGS && lineType == 1) cout << "C Full delete: " << strOss << endl;
                  }
                }
              }
              oss.str("");
            }
          }
          
		      /// Flush changes to DB
		      cout << " Flushing... ";
          dbw_flush(db);
          cout << "done" << endl;
		    }
      
        /// DB compression
        //cout << "DB compaction... " << flush;
        //dbw_compact(db);
        //cout << "done" << endl;
        
        cout << "----- COMPRESSION END now -----" << endl;
      
        dateLast = dateToHold;
        /// Release the mutex
        mutex.unlock();
      }
      
      /// Sleep for 10 minutes
      boost::this_thread::sleep(boost::posix_time::seconds(20));
    }
  } catch(boost::thread_interrupted &ex) {
    cout << "done" << endl;
  }
}

/*!
 * \fn void readLogThread(const Config c, unsigned long readPos)
 * \brief Do a continuous read of a file and call the line analyser.
 *
 * \param[in] c Config object containing the path/name of file to read.
 * \param[in] readPos Position in file to read (default: 0).
 */
void readLogThread(const Config c, unsigned long readPos) {
  string data;
  struct tm * timeinfo;
  time_t now;
  char buffer[80];
  ostringstream oss;
  int wait_time = 5; // wait time of 5 seconds if first read from log file
  
  ifstream posFileIn ("bin/mwa.pos");
  if (posFileIn.is_open()) {
    if (posFileIn.good()) {
      getline (posFileIn, data);
      stringstream(data) >> readPos;
    }
    posFileIn.close();
  } else cout << "Unable to open pos file." << endl;
  
  try {
    while(true) {
      if (readPos != 0) {
        wait_time = c.LOGS_READ_INTERVAL;
      }
      
      boost::this_thread::sleep(boost::posix_time::seconds(wait_time)); // interruptible
      
      /// Write to file atomically
      if (! mutex.try_lock()) {
        continue;
      }
      
      oss << c.LOG_FILE_PATH;
      now = time(0);
      timeinfo = localtime(&now);
      
      /// File ext format date :
      if (c.LOG_FILE_FORMAT == "timestamp") {
        time_t midnight = now / 86400 * 86400; // seconds
        oss << midnight;
      } else if (c.LOG_FILE_FORMAT == "date") {
        strftime (buffer, 11, "%Y-%m-%d", timeinfo);
        oss << buffer;
      }
      //--strftime (buffer, 80, "%c", timeinfo);
      //--cout << '\r' << setfill(' ') << setw(150) << '\r' << buffer << " - READ LOG (" << oss.str() << "): starting at " << readPos << flush;
      
      /// Reconstruct list of modules
      set<string> setModules;
      set<string>::iterator it;
      getDBModules(setModules, KEY_MODULES);
    
      readPos = readLogFile(c, oss.str(), setModules, readPos);
      //--cout << " until " << readPos << "." << flush;
      oss.str("");
      
      /// Save to pos file in case of error / server shutdown...
      ofstream posFileOut ("bin/mwa.pos");
      if (posFileOut.is_open()) {
        posFileOut << readPos << "\n";
        posFileOut.close();
      } else cout << "Unable to save pos to file" << endl;
      
      /// Update list of modules in DB
      string modules = "";
      for(it=setModules.begin(); it!=setModules.end(); it++) {
        modules += *it + "/";
      }
      dbw_remove(db, KEY_MODULES);
      dbw_add(db, KEY_MODULES, modules);
      
      /// Released the mutex
      mutex.unlock();
    }
  } catch(boost::thread_interrupted &ex) {
    cout << "done" << endl;
  }
}

/*!
 * \fn void handler_function(int signum)
 * \brief Handler to close properly db and threads.
  *
 * \param[in] signum Signal to catch
 */
void handler_function(int signum) {
  quit = true;
}

/*!
 * \fn int main (int argc, char* argv[])
 * \brief Main server function.
 *
 * \return EXIT_SUCCESS Full stop of the server.
 */
int main(int argc, char* argv[]) {
  boost::thread cThread, rThread;
  
  /// Announce yourself
  struct tm * timeinfo;
  time_t now;
  char buffer[80];
  now = time(0);
  timeinfo = localtime(&now);
  strftime (buffer, 80, "%c", timeinfo);
  cout << "Welcome to mooWApp." << endl << buffer << "." << endl;
  
  quit = false;
  
  /// Read configuration file
  
  /// Open the database
  db = dbw_open(c.DB_PATH, c.DB_NAME);
  if (db == NULL) {
    cout << "DB not opened. Exit program." << endl;
    return 1;
  }
  
  /// Attach handler for SIGINT
  signal(SIGINT, handler_function);
  
  /// DB Compact task set-up
  if (c.COMPRESSION) {
    cout << "DB task start..." << endl;
    cThread = boost::thread(compressionThread, c);
  }

  /// Start reading file
  cout << "Read file task start..." << endl;
  unsigned long readPos = 0;
  rThread = boost::thread(readLogThread, c, readPos);
  
  /// Json web server set-up
  const char *soptions[] = {"listening_ports", c.LISTENING_PORT.c_str(), NULL};
  cout << "Server now listening on " << c.LISTENING_PORT << endl;
  ctx = mg_start(&callback, NULL, soptions);
  
  /// Wait until shutdown with SIGINT
  while(!quit) {
    sleep(1);
  }
  // For debug purpose
  //getchar();  // Wait until user hits "enter" or any car
  
  /// Stop properly
  now = time(0); // Get date
  timeinfo = localtime(&now);
  strftime (buffer, 80, "%c", timeinfo);
  cout << buffer << ". Stoping server... " << flush;
  mg_stop(ctx);
  cout << "done" << endl << "Stoping LOG Thread... " << flush;
  rThread.interrupt();
  rThread.join();
  
  if (c.COMPRESSION) {
    cout << "Stoping Compression Thread... " << flush;
    cThread.interrupt();
    cThread.join();
  }
  cout << "Closing DB... " << flush;
  /// DB Release
  dbw_close(db);
  cout << "done" << endl;
  cout << "Good bye." << endl;
  
  return EXIT_SUCCESS;
}
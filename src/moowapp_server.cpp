/*!
 * \file moowapp_server.cpp
 * \brief Web Statistics DB Server aka : mooWApp
 * \author Xavier ETCHEBER
 * \version 0.2.1
 */

#include <iostream>
#include <string>
#include <cassert>
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
Db *db = NULL;
Config c; //-- Read configuration file
boost::mutex mutex; // Mutex for thread blocking
struct mg_context *ctx;
boost::thread cThread, rThread;

/*!
 * \fn int getDBModules(set<string> &setModules)
 * \brief Return a set of web modules stored in DB.
 *
 * \param setModules A set to store the web modules names.
 */
int getDBModules(set<string> &setModules) {
  //-- Create a set of all known applications in DB
  string strModules = dbw_get(db, "modules");
  
  if (strModules.length() <= 0)
    return 1;
  
  boost::split(setModules, strModules, boost::is_any_of("/"));
  setModules.erase(""); // Delete empty module
  
  if (c.DEBUG_APP_OTHERS || c.DEBUG_LOGS) cout << endl << "Known modules (" << setModules.size() << ") :" << strModules << endl;
  return 0;
}

/*!
 * \fn void statsAddSumRow(vector< pair<string, map<int, int> > > &vRes, int setDateSize)
 * \brief Insert in front of the vector in param, a SUM of visits by days
 *
 * \param vRes The vector where the SUM will be added.
 * \param nbResWithOffset A number of days
 * \param offset A number of stating day for the answer
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
 * \param vRes The vector of all stats grouped by module and stored by day.
 * \param response Response string in JSON format (only part of JSON answer will be returned). Should by empty at call.
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
 * \fn static void get_qsvar(const struct mg_request_info *request_info, const char *name, char *dst, size_t dst_len)
 * \brief Get a value of particular form variable.
 *
 * \param request_info Information about HTTP request.
 * \param name Variable name to decode from the buffer.
 * \param dst Destination buffer for the decoded variable.
 * \param dst_len Length of the destination buffer.
 */
static void get_qsvar(const struct mg_request_info *request_info,
                      const char *name, char *dst, size_t dst_len)
{
  const char *qs = request_info->query_string;
  mg_get_var(qs, strlen(qs == NULL ? "" : qs), name, dst, dst_len);
}


void filteringPeriod(const struct mg_request_info *ri, int i, string &strYearMonth, set<string> &setDateToKeep) {
  char strAppDays[61]; // Days in the month, starting at 0. Ex: 0-30 or 0-2,4,6-30
  ostringstream oss;
  
  // Get periods for that project (filtering)
  oss << "p_" << i << "_d";
  get_qsvar(ri, oss.str().c_str(), strAppDays, sizeof(strAppDays));
  oss.str("");
  if (strAppDays[0] == '\0') sprintf(strAppDays, "1-31"); // Default value : complete month
  //if (c.DEBUG_REQUESTS) cout << " days=" << strAppDays;

  //-- Store only date to be returned
  if (strcmp(strAppDays, "1-31") != 0) {
  
    // Split sequences separated by coma ; Ex 1-4,6,8-12,15
    vector<string> vectStrComa;
    boost::split(vectStrComa, strAppDays, boost::is_any_of(","));
    for(vector<string>::iterator tok_iter = vectStrComa.begin(); tok_iter != vectStrComa.end(); ++tok_iter) {
    
      size_t found = (*tok_iter).find("-");
      if (found != string::npos) {
        // Split sequences separated by - ; Ex: 3-30
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
        try {
          oss << strYearMonth << std::setw(2) << std::setfill('0') << *tok_iter;
          //if (c.DEBUG_REQUESTS) cout <<  " Added: " << oss.str();
          setDateToKeep.insert(oss.str());
          oss.str("");
        } catch(boost::bad_lexical_cast &) {}
      }
    }
  }
}

/*!
 * \fn static bool handle_jsonp(struct mg_connection *conn, const struct mg_request_info *request_info)
 * \brief Tell if the request is a JSON call
 *
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 * \return true if "callback" param is present in query string, false otherwise.
 */
static bool handle_jsonp(struct mg_connection *conn,
                        const struct mg_request_info *request_info)
{
  char cb[64];
  get_qsvar(request_info, "callback", cb, sizeof(cb));
  if (cb[0] != '\0') {
    mg_printf(conn, "%s(", cb);
  }
  return cb[0] == '\0' ? false : true;
}

/*!
 * \fn static void stats_app_intra(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_intra context.
 *
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 */
static void stats_app_intra(struct mg_connection *conn,
                            const struct mg_request_info *ri)
{
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, offset;
  unsigned int iVisit, minVisit;
  char strDates[11];   // Number of dates. Ex: 60
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strOffset[11];  // Date offset. Ex: 60
  char strModules[11]; // Number of modules. Ex: 4
  char strApplication[65];  // Application name. Ex: Calendar
  char strModule[65];  // Modules name. Ex: gerer_connaissance
  char strMode[4];     // Mode. Ex: app or all
  char strType[2];     // Mode. Ex: 1:visits, 2:views, 3:statics
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
  time_t tStamp;
  struct tm * timeinfo;
  map<int, string> mapDate;
  map<int, string>::iterator itm;
  set<string> setModules, setOtherModules;
  set<string>::iterator its;
  
  //-- Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");

  //-- Get parameters in request.
  get_qsvar(ri, "mode", strMode, sizeof(strMode));
  assert(strMode[0] != '\0');
  mode = string(strMode);
  if (mode == "all") {
    get_qsvar(ri, "apps", strModules, sizeof(strModules));
    getDBModules(setOtherModules);
  } else {
    get_qsvar(ri, "modules", strModules, sizeof(strModules));
  }
  get_qsvar(ri, "dates", strDates, sizeof(strDates));
  assert(strDates[0] != '\0');
  sscanf(strDates, "%d", &max);
  get_qsvar(ri, "offset", strOffset, sizeof(strOffset));
  assert(strOffset[0] != '\0');
  sscanf(strOffset, "%d", &offset);
  get_qsvar(ri, "type", strType, sizeof(strType));
  assert(strType[0] != '\0');
  
  //-- Set each date to according offset in response.
  max += offset;
  int ii = 0, iii = 0;
  for(i = offset; i < max; i++, ii++) {
    if (ii!=0 && ii%6 == 0) { ii = 0; iii+=10; }
    oss << "d_" << (offset + ii + iii);
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0') {
      //cout << "ici: i=" << i << " ii=" << ii << " iii=" << iii << " => " << (offset + ii + iii) << " strDate=" << strDate << endl;
      mg_printf(conn, "\"%d\":\"%s\",", (offset + ii + iii), strDate);
      
      // Convert timestamp to Y-m-d
      ss.str(strDate);
      ss >> tStamp; //Ex: 1303639200;
      timeinfo = localtime(&tStamp);
      strftime(strDate, 31, "%Y-%m-%d", timeinfo);
      mapDate.insert( pair<int,string>((offset + ii + iii), strDate) );
    }
  }
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  strftime(strDate, 31, "%A %d %B", timeinfo);
  // Put mode and label after the last date
  i = offset + ii + iii;
  mg_printf(conn, "\"%d\":\"intra\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Build visits stats in response for each modules.
  vector< pair<string, map<int, int> > > vRes;
  sscanf(strModules, "%d", &nbApps);
  if (c.DEBUG_REQUESTS) cout << "stats_app_intra - with " << nbApps << flush;
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (mode == "all") {
      oss << "p_" << i;
      get_qsvar(ri, oss.str().c_str(), strApplication, sizeof(strApplication));
      oss.str("");
      if (strApplication[0] == '\0') continue;
      if (c.DEBUG_REQUESTS && i==0) cout << " apps." << endl;

      // Get nb module of that app in request
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModules, sizeof(strModules));
      oss.str("");
      if (strModules[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << " with " << strModules << " modules in app " << strApplication << " [" << flush;

      // Loop to put modules from request in a set
      setModules.clear();
      
      sscanf(strModules, "%d", &nbModules);
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
        oss.str("");
        if (strModule[0] == '\0') continue;
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
          iVisit = 0;
          if (visit.length() > 0) {
            // Update nb visit of the app for this day
            sscanf(visit.c_str(), "%d", &iVisit);
            minVisit += iVisit;
          }
          // Return last nb visit
          //if (c.DEBUG_REQUESTS && strcmp("xxx", strApplication)==0) cout << " visits for " << oss.str() << " (" << (*itm).first << ")= " << iVisit << endl; 
          oss.str("");
        }  
        mapResMod.insert(pair<int, int>((*itm).first, minVisit));
      }
    
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      if (c.DEBUG_REQUESTS && i==0) cout << " modules in app." << endl;
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
      oss.str("");
      if (strModule[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << " - module=" << strModule << endl;
      
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
        mapResMod.insert(pair<int, int>((*itm).first, iVisit));
      }
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  //-- In all mode, add an "Others" application
  if (mode == "all" && setOtherModules.size() > 0) {
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
        iVisit = 0;
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
          sscanf(visit.c_str(), "%d", &iVisit);
          minVisit += iVisit;
        }
        // Return last nb visit
        //if (c.DEBUG_REQUESTS && strcmp("xxx", strApplication)==0) cout << " visits for " << oss.str() << " (" << (*itm).first << ")= " << iVisit << endl; 
        oss.str("");
      }
      if (c.DEBUG_APP_OTHERS && itm == mapDate.begin()) cout << endl;
      mapResMod.insert(pair<int, int>((*itm).first, minVisit));
    }
    vRes.push_back(make_pair("Others", mapResMod));
  }
  
  //-- Add a SUM row serie
  statsAddSumRow(vRes, (max-offset), offset); // 36 a day without offset
  
  //-- Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  //-- Set end JSON string in response.
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
 * \fn static void stats_app_day(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_day context.
 *
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 */
static void stats_app_day(struct mg_connection *conn,
                            const struct mg_request_info *ri)
{
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, k;
  unsigned int iVisit, hourVisit;
  char strDates[11];   // Number of dates. Ex: 60
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strModules[11]; // Number of modules. Ex: 4
  char strApplication[65];  // Application name. Ex: Calendar
  char strModule[65];  // Modules name. Ex: module_test_1
  char strMode[4];     // Mode. Ex: app or all
  char strType[2];     // Mode. Ex: 1:visits, 2:views, 3:statics
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
  time_t tStamp;
  struct tm * timeinfo;
  set<string> setModules, setOtherModules;
  set<string>::iterator it;
  unsigned int nbVisitForApp;

  //-- Get parameters in request.
  get_qsvar(ri, "mode", strMode, sizeof(strMode));
  assert(strMode[0] != '\0');
  mode = string(strMode);
  if (mode == "all") {
    get_qsvar(ri, "apps", strModules, sizeof(strModules));
    getDBModules(setOtherModules);
  } else {
    get_qsvar(ri, "modules", strModules, sizeof(strModules));
  }
  get_qsvar(ri, "dates", strDates, sizeof(strDates));
  assert(strDates[0] != '\0');
  get_qsvar(ri, "type", strType, sizeof(strType));
  assert(strType[0] != '\0');
  
  //-- Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  //-- Set each date in response.
  i = 0;
  sscanf(strDates, "%d", &max);
  for(max += i; i < max; i++) {
    oss << "d_" << i;
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0') {
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
    }
  }
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%A %d %B", timeinfo);
  mg_printf(conn, "\"%d\":\"day\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  // Convert timestamp to Y-m-d  
  strftime(strDate, 31, "%Y-%m-%d", timeinfo);
  
  //-- Build visits stats in response for each modules or app.
  vector< pair<string, map<int, int> > > vRes;
  sscanf(strModules, "%d", &nbApps);
  if (c.DEBUG_REQUESTS) cout << "stats_app_day - with " << nbApps << flush;
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (mode == "all") {
      oss << "p_" << i;
      get_qsvar(ri, oss.str().c_str(), strApplication, sizeof(strApplication));
      oss.str("");
      if (strApplication[0] == '\0') continue;
      if (c.DEBUG_REQUESTS && i==0) cout << " apps." << endl;

      // Get nb module of that app in request
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModules, sizeof(strModules));
      oss.str("");
      if (strModules[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << " with " << strModules << " modules in app " << strApplication << " [" << flush;

      // Loop to put modules from request in a set
      setModules.clear();
      
      sscanf(strModules, "%d", &nbModules);
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
        oss.str("");
        if (strModule[0] == '\0') continue;
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);

        // Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;

      //-- and each module in an app
      hourVisit = 0;
      for(int l=k=0, max=DB_TIMES_SIZE;l<max;l++) {
        //-- Get nb visit from DB
        for(it=setModules.begin(); it!=setModules.end(); it++) {
          // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
          oss << *it << '/' << strType << "/" << strDate << '/' << dbTimes[l];
          // Search Key (oss) in DB
          visit = dbw_get(db, oss.str());
          iVisit = 0;
          if (visit.length() > 0) {
            sscanf(visit.c_str(), "%d", &iVisit);
            ////if (*it == "bureau") cout << oss.str() << " = " << iVisit << endl;
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
      // Return last nb visit
      mapResMod.insert(pair<int, int>(23, hourVisit));
    
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      if (c.DEBUG_REQUESTS && i==0) cout << " modules in app." << endl;
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
      oss.str("");
      if (strModule[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << " - module=" << strModule;
    
      //-- Get nb visit from DB
      k = hourVisit = 0;
      for(int l=0, max=DB_TIMES_SIZE;l<max;l++) {
        // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
        oss << strModule << '/' << strType << "/" << strDate << '/' << dbTimes[l];
        ////if (c.DEBUG_REQUESTS && l==0) cout << " visits for " << oss.str() << endl;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        iVisit = 0;
        if (visit.length() > 0) {
          sscanf(visit.c_str(), "%d", &iVisit);
          ////if (c.DEBUG_REQUESTS) cout << oss.str() << " = " << iVisit << endl;
        }
        int timeVal = 0;
        sscanf(dbTimes[l].c_str(), "%d", &timeVal);
        if (floor(timeVal/10) == k) {
          hourVisit += iVisit;
          ////cout << "hourVisit=" << hourVisit << endl;
        } else {
          ////if (c.DEBUG_REQUESTS) cout << " /" << dbTimes[l] << " k=" << k << " hourVisit=" << hourVisit << endl;
          // Return nb visit
          mapResMod.insert(pair<int, int>(k, hourVisit));
          hourVisit = iVisit;
          k = floor(timeVal/10);
        }
        oss.str("");
      } 
      ////if (c.DEBUG_REQUESTS) cout << "LAST k=" << k << " hourVisit=" << hourVisit << endl;
      if (c.DEBUG_REQUESTS) cout << endl;
      // Return last nb visit
      mapResMod.insert(pair<int, int>(23, hourVisit));
      
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  //-- In all mode, add an "Others" application
  if (mode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    hourVisit = 0;
    //-- Get nb visit from DB
    for(int l = k = 0, max=DB_TIMES_SIZE;l<max;l++) {
      int timeVal = 0;
      sscanf(dbTimes[l].c_str(), "%d", &timeVal);
      for(it=setOtherModules.begin(), nbVisitForApp = 0; it!=setOtherModules.end(); it++) {
        if (c.DEBUG_APP_OTHERS && l == 0) cout << *it << ", ";
        // Build Key ex: "creer_modifier_retrocession/1/2011-04-24/150";
        oss << *it << '/' << strType << "/" << strDate << '/' << timeVal;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        iVisit = 0;
        if (visit.length() > 0) {
          sscanf(visit.c_str(), "%d", &iVisit);
          nbVisitForApp += iVisit;
          ////if (c.DEBUG_APP_OTHERS && *it == "bureau") cout << oss.str() << " = " << iVisit << endl;
        }
        oss.str("");
      }
      if (c.DEBUG_APP_OTHERS && l == 0) cout << endl;
      if (floor(timeVal/10) == k) {
        hourVisit += nbVisitForApp;
      } else {
        ///if (c.DEBUG_APP_OTHERS) cout << "HERE k=" << k << " dbTimes[i]=" << timeVal << " hourVisit=" << hourVisit << endl;
        // Return nb visit
        mapResMod.insert(pair<int,int>(k, hourVisit));
        hourVisit = nbVisitForApp;
        k = floor(timeVal/10);
      }
    }
    // Return last nb visit
    mapResMod.insert(pair<int,int>(23, hourVisit));
    oss.str("");
    vRes.push_back(make_pair("Others", mapResMod));
  }
  
  //-- Add a SUM row serie
  statsAddSumRow(vRes, 24, 0); // 24hours a day without offset
  
  //-- Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  //-- Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn static void stats_app_week(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_week context.
 *
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 */
static void stats_app_week(struct mg_connection *conn,
                            const struct mg_request_info *ri)
{
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, offset;
  char strDates[11];   // Number of dates. Ex: 31
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strOffset[11];  // Date offset. Ex: 11
  char strModules[11]; // Number of modules. Ex: 4
  char strApplication[65];  // Application name. Ex: Calendar
  char strModule[65];  // Modules name. Ex: gerer_connaissance
  char strMode[4];     // Mode. Ex: app or all
  char strType[2];     // Mode. Ex: 1:visits, 2:views, 3:statics
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
  string date;
  time_t tStamp;
  struct tm * timeinfo;
  set<string> setDate, setModules, setOtherModules;
  set<string>::iterator it, itt;
  unsigned int nbVisitForApp;

  //-- Get parameters in request.
  get_qsvar(ri, "mode", strMode, sizeof(strMode));
  assert(strMode[0] != '\0');
  mode = string(strMode);
  if (mode == "all") {
    get_qsvar(ri, "apps", strModules, sizeof(strModules));
    getDBModules(setOtherModules);
  } else {
    get_qsvar(ri, "modules", strModules, sizeof(strModules));
  }
  get_qsvar(ri, "dates", strDates, sizeof(strDates));
  assert(strDates[0] != '\0');
  sscanf(strDates, "%d", &max);
  get_qsvar(ri, "offset", strOffset, sizeof(strOffset));
  assert(strOffset[0] != '\0');
  sscanf(strOffset, "%d", &offset);
  get_qsvar(ri, "type", strType, sizeof(strType));
  assert(strType[0] != '\0');
  
  //-- Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  //-- Set each date to according offset in response.
  max += offset;
  for(i = offset; i < max; i++) {
    oss << "d_" << i;
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0') {
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
      
      // Convert timestamp to Y-m-d
      boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
      boost::gregorian::date d = pt.date();
      date = boost::gregorian::to_iso_extended_string(d);
      setDate.insert(date);
    }
  }
  
  //-- Save Year-Month for later
  size_t found = date.find_last_of("-");
  string strYearMonth = date.substr(0, found+1);
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%B %Y", timeinfo); // Depend one request intra, day, week, month, year
  mg_printf(conn, "\"%d\":\"month\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Build visits stats in response for each modules.
  vector< pair<string, map<int, int> > > vRes;
  sscanf(strModules, "%d", &nbApps);
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (mode == "all") {
      oss << "p_" << i;
      get_qsvar(ri, oss.str().c_str(), strApplication, sizeof(strApplication));
      oss.str("");
      if (strApplication[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << "stats_app_week - app=" << strApplication;
      
      //-- Filter for days
      set<string> setDateToKeep;
      filteringPeriod(ri, i, strYearMonth, setDateToKeep);
      
      // Get nb module of that app in request
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModules, sizeof(strModules));
      oss.str("");
      if (strModules[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << " with " << strModules << " modules in app [" << flush;
      
      // Loop to put modules from request in a set
      setModules.clear();
      sscanf(strModules, "%d", &nbModules);
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
        oss.str("");
        if (strModule[0] == '\0') continue;
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);
        
        // Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;
        
      //-- and loop for each dates.
      sscanf(strOffset, "%d", &j);
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
            int iVisit = 0;
            if (visit.length() > 0) {
              // Update nb visit of the app for this day
              sscanf(visit.c_str(), "%d", &iVisit);
              nbVisitForApp += iVisit;
            }
          }
        }
        if (c.DEBUG_REQUESTS) cout << *it << " => " << nbVisitForApp << " visits." << endl;
        it++;
        
        // Return nb visit
        ///if (nbVisitForApp != 0){
          mapResMod.insert(pair<int, int>(j, nbVisitForApp));
        ///}
      }
      
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
      oss.str("");
      if (strDate[0] == '\0') continue;
      
      if (c.DEBUG_REQUESTS) cout << "stats_app_week: " << strModule << endl;
    
      //-- and each dates.
      sscanf(strOffset, "%d", &j);
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
        mapResMod.insert(pair<int, int>(j, boost::lexical_cast<int>(visit)));
      }
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  //-- In all mode, add an "Others" application
  if (mode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    sscanf(strOffset, "%d", &j);
    for(it=setDate.begin(); it!=setDate.end(); j++, it++) {
      for(itt=setOtherModules.begin(), nbVisitForApp = 0; itt!=setOtherModules.end(); itt++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/2011-04-24";
        oss << *itt << '/' << strType << "/" << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        int iVisit = 0;
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
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
  
  //-- Add a SUM row serie
  statsAddSumRow(vRes, max, offset);
  
  //-- Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  //-- Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
   response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn static void stats_app_month(struct mg_connection *conn, const struct mg_request_info *ri)
 * \brief Build an HTTP response for the /stats_app_month context.
 *
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 */
static void stats_app_month(struct mg_connection *conn,
                            const struct mg_request_info *ri)
{
  bool is_jsonp;
  int i, j, max, nbApps, nbModules, offset;
  char strDates[11];   // Number of dates. Ex: 31
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strOffset[11];  // Date offset. Ex: 11
  char strModules[11]; // Number of modules. Ex: 4
  char strApplication[65];  // Application name. Ex: Calendar
  char strModule[65];  // Modules name. Ex: module_test_1
  char strMode[4];     // Mode. Ex: app or all
  char strType[2];     // Mode. Ex: 1 or 2 or 3
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
  string date;
  time_t tStamp;
  struct tm * timeinfo;
  set<string> setDate, setModules, setOtherModules;
  set<string>::iterator it, itt;
  unsigned int nbVisitForApp;

  //-- Get parameters in request.
  get_qsvar(ri, "mode", strMode, sizeof(strMode));
  assert(strMode[0] != '\0');
  mode = string(strMode);
  if (mode == "all") {
    get_qsvar(ri, "apps", strModules, sizeof(strModules));
    getDBModules(setOtherModules);
  } else {
    get_qsvar(ri, "modules", strModules, sizeof(strModules));
  }
  assert(strModules[0] != '\0');
  get_qsvar(ri, "dates", strDates, sizeof(strDates));
  assert(strDates[0] != '\0');
  sscanf(strDates, "%d", &max);
  get_qsvar(ri, "offset", strOffset, sizeof(strOffset));
  assert(strOffset[0] != '\0');
  sscanf(strOffset, "%d", &offset);
  get_qsvar(ri, "type", strType, sizeof(strType));
  assert(strType[0] != '\0');
  
  //-- Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_write(conn, "[{", 2);
  
  if (c.DEBUG_REQUESTS) cout << "nb=" << strModules << endl;
  
  //-- Create a set for the Dates to loop easily
  max += offset;
  for(i = offset; i < max; i++) {
    oss << "d_" << i;
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0') {
      // Convert timestamp to Y-m-d
      boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
      boost::gregorian::date d = pt.date();
      date = boost::gregorian::to_iso_extended_string(d);
      setDate.insert(date);
      //-- Set each date to according offset in response.
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
    }
  }
  
  //-- Save Year-Month for later
  size_t found = date.find_last_of("-");
  string strYearMonth = date.substr(0, found+1);
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%B %Y", timeinfo); // Depend one request intra, day, week, month, year
  mg_printf(conn, "\"%d\":\"month\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Build visits stats in response for each modules or app.
  vector< pair<string, map<int, int> > > vRes;
  sscanf(strModules, "%d", &nbApps);
  for(i = 0; i < nbApps; i++) {
    map<int, int> mapResMod;
    if (mode == "all") {
      oss << "p_" << i;
      get_qsvar(ri, oss.str().c_str(), strApplication, sizeof(strApplication));
      oss.str("");
      if (strApplication[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << "stats_app_month - app=" << strApplication;
      
      //-- Filter for days
      set<string> setDateToKeep;
      filteringPeriod(ri, i, strYearMonth, setDateToKeep);
      
      // Get nb module of that app in request
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModules, sizeof(strModules));
      oss.str("");
      if (strModules[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << " with " << strModules << " modules in app [" << flush;
      
      // Loop to put modules from request in a set
      setModules.clear();
      sscanf(strModules, "%d", &nbModules);
      for(j = 0; j < nbModules; j++) {
        oss << "m_" << i << "_" << j;
        get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
        oss.str("");
        if (strModule[0] == '\0') continue;
        if (c.DEBUG_REQUESTS) cout << strModule << ", " << flush;
        setModules.insert(strModule);
        
        // Remove this module from the whole app list
        setOtherModules.erase(strModule);
      }
      if (c.DEBUG_REQUESTS) cout << "]" << endl;
        
      //-- and loop for each dates.
      sscanf(strOffset, "%d", &j);
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
            int iVisit = 0;
            if (visit.length() > 0) {
              // Update nb visit of the app for this day
              sscanf(visit.c_str(), "%d", &iVisit);
              nbVisitForApp += iVisit;
            }
          }
        }
        if (c.DEBUG_REQUESTS) cout << *it << " => " << nbVisitForApp << " visits." << endl;
        it++;
        
        // Return nb visit
        ///if (nbVisitForApp != 0){
          mapResMod.insert(pair<int, int>(j, nbVisitForApp));
        ///}
      }
      
      vRes.push_back(make_pair(strApplication, mapResMod));
    }
    else {
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
      oss.str("");
      if (strModule[0] == '\0') continue;
      if (c.DEBUG_REQUESTS) cout << "stats_app_month - module=" << strModule << endl;
      
      //-- and each dates.
      sscanf(strOffset, "%d", &j);
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
          mapResMod.insert(pair<int,int>(j, boost::lexical_cast<int>(visit)));
        } else {
          mapResMod.insert(pair<int,int>(j, 0));
        }
      }
      
      vRes.push_back(make_pair(strModule, mapResMod));
    }
  }
  
  //-- In all mode, add an "Others" application
  if (mode == "all" && setOtherModules.size() > 0) {
    map<int, int> mapResMod;
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    sscanf(strOffset, "%d", &j);
    for(it=setDate.begin(); it!=setDate.end(); j++, it++) {
      for(itt=setOtherModules.begin(), nbVisitForApp = 0; itt!=setOtherModules.end(); itt++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/2011-04-24";
        oss << *itt << '/' << strType << "/" << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        int iVisit = 0;
        if (visit.length() > 0) {
          // Update nb visit of the app for this day
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
  
  //-- Add a SUM row serie
  statsAddSumRow(vRes, setDate.size(), 0);
  
  //-- Construct response
  string response = "";
  statsConstructResponse(vRes, response);
  
  //-- Set end JSON string in response.
  response += "]";
  if (is_jsonp) {
    response += ")";
  }
  mg_write(conn, response.c_str(), response.length());
}

/*!
 * \fn static void get_error(struct mg_connection *conn, const struct mg_request_info *request_info)
 * \brief Build an HTTP error response.
 *
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 */
static void get_error(struct mg_connection *conn,
                      const struct mg_request_info *request_info)
{
  mg_printf(conn, "HTTP/1.1 %d XX\r\n"
            "Connection: close\r\n\r\n", request_info->status_code);
  mg_printf(conn, "Error: [%d]", request_info->status_code);
}

static const struct uri_config {
  enum mg_event event;
  const char *uri;
  void (*func)(struct mg_connection *, const struct mg_request_info *);
} uri_config[] = {
  {MG_NEW_REQUEST, "/stats_app_intra", &stats_app_intra},
  {MG_NEW_REQUEST, "/stats_app_day", &stats_app_day},
  {MG_NEW_REQUEST, "/stats_app_week", &stats_app_week},
  {MG_NEW_REQUEST, "/stats_app_month", &stats_app_month},
  {MG_HTTP_ERROR, "", &get_error}//,
  //{0, NULL, NULL}
};

/*!
 * \fn static void *callback(enum mg_event event, struct mg_connection *conn, const struct mg_request_info *request_info)
 * \brief Call the right function depending on the request context.
 *
 * \param event Which event has been triggered.
 * \param conn Opaque connection handler.
 * \param request_info Information about HTTP request.
 */
static void *callback(enum mg_event event,
                      struct mg_connection *conn,
                      const struct mg_request_info *request_info)
{
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
 * \fn void compressionThread()
 * \brief Compress the stats DB at a precise time once a day until 7 day from today.
 *
 * \param c Config file.
 */
void compressionThread(const Config c) {
  uint64_t i;
  int dayVisit, monthNumber;
  ostringstream oss;
  string visit;
  string strOss;
  set<string> setModules;
  set<string>::iterator it;
  struct tm * timeinfo;
  time_t now;
  char buffer[80];
  
  // At the first start do a compression from the first day of the year
  boost::gregorian::date dateNow(boost::gregorian::day_clock::universal_day());
  boost::gregorian::date dateLast(dateNow.year(), boost::gregorian::Jan, 1);
  
  // Hold the delay for non compressed stats
  boost::gregorian::date_duration dd_week(c.DAYS_FOR_DETAILS);
  
  /// FOR DEBUG purpose USE minutes + 1
  //boost::posix_time::ptime t = boost::posix_time::second_clock::universal_time() + boost::posix_time::minutes(LOGS_COMPRESSION_INTERVAL);
  /// FOR REAL usage USE a specific date/time : the next day at 3 o'clock
  boost::gregorian::date_duration dd(1);
  boost::posix_time::ptime t(boost::gregorian::day_clock::universal_day() + dd, boost::posix_time::time_duration(3,0,0));
  
  try {
    while(true)
    {
      boost::gregorian::date today(boost::gregorian::day_clock::universal_day());
      boost::gregorian::date dateToHold(today - dd_week);
      boost::posix_time::ptime timeNow(boost::posix_time::second_clock::universal_time());
      //cout << "Obj:" << boost::posix_time::to_simple_string(t) << " & now:" << boost::posix_time::to_simple_string(timeNow) << endl;
      if (timeNow >= t) {
        /// FOR DEBUG
        //t += boost::posix_time::minutes(2*c.LOGS_COMPRESSION_INTERVAL);
        /// FOR Real use +1 day
        t += boost::posix_time::hours(24);
        
        // Compression to file atomically
        boost::mutex::scoped_lock lock(mutex);
        cout << "----- COMPRESSION RUNNING now -----" << endl;
        
        // Get date
        now = time(0);
        timeinfo = localtime(&now);
        strftime (buffer, 80, "%c", timeinfo);
        cout << buffer << endl;
        
        // Reconstruct list of modules
        setModules.clear();
        getDBModules(setModules);
        
        //-- Reloop thru all days to j-x in order to remove details and store days only
        // Loop thru day since last parsing
        boost::gregorian::day_iterator ditr(dateLast);
        for (;ditr <= today; ++ditr) {
          //produces "C: 2011-Nov-04", "C: 2011-Nov-05", ...
          cout << "C: " << to_simple_string(*ditr) << flush;
          if (ditr <= dateToHold) {
            cout << " R." << endl;
          } else {
            cout << endl;
          }
          monthNumber = ditr->month();
          
          // Check to see if this thread has been interrupted before going into each days of the curent month
          boost::this_thread::interruption_point();
        
          // Loop thru modules
          for(it=setModules.begin(); it!=setModules.end(); it++) {
            for(int lineType = 1; lineType <= 2; lineType++) {
              // 1=> URL with -event.do
              // 2 => URL with .do and without -event.do
              dayVisit = 0;
              oss << *it << '/' << lineType << '/' << ditr->year() << "-" << setfill('0') << setw(2) << monthNumber
                  << "-" << setfill('0') << setw(2) << ditr->day();
              strOss = oss.str();
              //if(c.DEBUG_LOGS && lineType == 1) cout << "C Searched: " << strOss << endl;
              for(i=0;i<DB_TIMES_SIZE;i++) {
                // Search Key in DB
                visit = dbw_get(db, strOss+'/'+dbTimes[i]);
                //if(c.DEBUG_LOGS) cout << "C Searched: " << strOss << '/' << dbTimes[i] << endl;
                if (visit.length() > 0) {
                  if (ditr <= dateToHold) {
                    // Delete the current Key in DB
                    dbw_remove(db, strOss+'/'+dbTimes[i]);
                  }
                  // Return nb visit
                  dayVisit += boost::lexical_cast<int>(visit);
                  if(c.DEBUG_LOGS && lineType == 1) cout << "C Found: " << strOss << '/' << dbTimes[i] << " = " << dayVisit << endl;
                }
              }
          
              if (dayVisit > 0) {
                // Add this day Key in DB
                if (dbw_add(db, strOss, boost::lexical_cast<string>(dayVisit))) {
                  if(c.DEBUG_LOGS && lineType == 1) cout << "C Added: " << strOss << " = " << dayVisit << endl;
                }
              }
              oss.str("");
            }
          }
        }
      
        // Dump DB
        dbw_flush(db);
        
        cout << "----- COMPRESSION END now -----" << endl;
      
        dateLast = dateToHold;
        // Here is released the scoped mutex automatically
      }
      
      ///FOR REAL USE 10 minutes
      boost::this_thread::sleep(boost::posix_time::minutes(10));
    }
  
  } catch(boost::thread_interrupted &ex) {
    // Dump DB
    dbw_flush(db);
      
    cout << "done" << endl;
  }
  return;
}

/*!
 * \fn void readLogThread(const Config c)
 * \brief Do a continuous read of a file and call the line analyser.
 *
 * \param c Config file containing the path/name of file to read.
 * \param readPos Position in file to read (default: 0).
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
      if (readPos != 0)
        wait_time = c.LOGS_READ_INTERVAL;
      
      boost::this_thread::sleep(boost::posix_time::seconds(wait_time)); // interruptible
      
      // Write to file atomically
      if (! mutex.try_lock()) {
        continue;
      }
      
      oss << c.LOG_FILE_PATH;
      now = time(0);
      timeinfo = localtime(&now);
      
      // File ext format date :
      if (c.LOG_FILE_FORMAT == "timestamp") {
        time_t midnight = now / 86400 * 86400; // seconds
        oss << midnight;
      } else if (c.LOG_FILE_FORMAT == "date") {
        strftime (buffer, 11, "%Y-%m-%d", timeinfo);
        oss << buffer;
      }
      strftime (buffer, 80, "%c", timeinfo);
      cout << '\r' << setfill(' ') << setw(150) << '\r' << buffer << " - READ LOG (" << oss.str() << "): starting at " << readPos << flush;
      
      //-- Reconstruct list of modules
      set<string> setModules;
      set<string>::iterator it;
      getDBModules(setModules);
    
      readPos = readLogFile(c, oss.str(), setModules, readPos);
      cout << " until " << readPos << "." << flush;
      oss.str("");
      
      // Save to pos file in case of error / server shutdown...
      ofstream posFileOut ("bin/mwa.pos");
      if (posFileOut.is_open()) {
        posFileOut << readPos << "\n";
        posFileOut.close();
      } else cout << "Unable to save pos to file" << endl;
      
      // Update list of modules in DB
      string modules = "";
      for(it=setModules.begin(); it!=setModules.end(); it++) {
        modules += *it + "/";
      }
      dbw_remove(db, "modules");
      dbw_add(db, "modules", modules);
      
      // Released the mutex
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
 * \param signum Signal to catch
 */
void handler_function(int signum) {
  struct tm * timeinfo;
  time_t now;
  char buffer[80];
  
  // Stop server and block activities during webserver close
  mutex.lock();
  
  // Get date
  now = time(0);
  timeinfo = localtime(&now);
  strftime (buffer, 80, "%c", timeinfo);
  cout << buffer << ". Stoping server... " << flush;
  mg_stop(ctx);
  
  cout << "done" << endl << "Stoping Compression Thread... " << flush;
  cThread.interrupt();
  cThread.join();
  cout << "Stoping LOG Thread... " << flush;
  rThread.interrupt();
  rThread.join();
  
  cout << "Closing DB... " << flush;
  //-- DB Release
  dbw_close(db);
  
  cout << "done" << endl << "Good bye." << endl;
  
  // Release mutex
  mutex.unlock();
  
  exit(0);
}

int main(int argc, char* argv[]) {
  // Announce yourself
  struct tm * timeinfo;
  time_t now;
  char buffer[80];
  now = time(0);
  timeinfo = localtime(&now);
  strftime (buffer, 80, "%c", timeinfo);
  cout << "Welcome to mooWApp." << endl << buffer << "." << endl;
  
  //-- Read configuration file
  
  //-- Open the database
  Db db_(NULL, 0);
  db = dbw_open(&db_, c.DB_PATH.c_str());
  if (db == NULL) {
    cout << "DB not opened. Exit program." << endl;
    return 1;
  }
  
  // Attach handler for SIGINT
  signal(SIGINT, handler_function);
  
  //-- DB Compact task set-up
  if (c.COMPRESSION) {
    cout << "DB task start..." << endl;
    cThread = boost::thread(&compressionThread, c);
  }

  //-- Start reading file
  cout << "Read file task start..." << endl;
  unsigned long readPos = 0;
  rThread = boost::thread(&readLogThread, c, readPos);
  
  //-- Json web server set-up
  const char *soptions[] = {"listening_ports", c.LISTENING_PORT.c_str(), NULL};
  cout << "Server now listening on " << c.LISTENING_PORT << endl;
  ctx = mg_start(&callback, NULL, soptions);
  
  // Wait until shutdown with SIGINT
  while(1) {
    sleep(1);
  }
  // For debug purpose
  //getchar();  // Wait until user hits "enter" or any car
  
  // Stop properly
  handler_function(1);
  
  return 0;
}

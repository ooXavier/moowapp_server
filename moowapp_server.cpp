/*!
 * \file moowapp_server.cpp
 * \brief Web Statistics DB Server aka : mooWApp
 * \author Xavier ETCHEBER
 * \version 0.1
 */

#include <iostream>
#include <string>
#include <cassert>
#include <sstream>
#include <set>
#include <vector> // Line log analyse

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
Db *db;
Config c; //-- Read configuration file
boost::mutex mutex; // Mutex for thread blocking

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
  
  if (c.DEBUG_APP_OTHERS || c.DEBUG_LOGS) cout << "Known modules (" << setModules.size() << ") :" << strModules << endl;
  return 0;
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
  int i, j, max, nbModules;
  char strDates[11];   // Number of dates. Ex: 60
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strOffset[11];  // Date offset. Ex: 60
  char strModules[11]; // Number of modules. Ex: 4
  char strModule[65];  // Modules name. Ex: gerer_connaissance
  char strMode[4];     // Mode. Ex: app or all
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
  time_t tStamp;
  struct tm * timeinfo;
  
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
  } else {
    get_qsvar(ri, "modules", strModules, sizeof(strModules));
  }
  get_qsvar(ri, "dates", strDates, sizeof(strDates));
  assert(strDates[0] != '\0');
  get_qsvar(ri, "offset", strOffset, sizeof(strOffset));
  assert(strOffset[0] != '\0');
  
  //-- Set each date to according offset in response.
  sscanf(strOffset, "%d", &i);
  sscanf(strDates, "%d", &max);
  for(max += i; i < max; i++) {
    oss << "d_" << i;
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0')
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
  }
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%A %d %B", timeinfo);
  mg_printf(conn, "\"%d\":\"intra\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Build visits stats in response for each modules.
  sscanf(strModules, "%d", &nbModules);
  for(j = 0; j < nbModules; j++) {
    oss << "m_" << j;
    get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
    oss.str("");
    if (strDate[0] != '\0')
      mg_printf(conn, "[\"%s\",{", strModule);
      
    //-- and each dates.
    sscanf(strOffset, "%d", &i);
    sscanf(strDates, "%d", &max);
    for(max += i; i < max; i++) {
      oss << "d_" << i;
      get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
      oss.str("");
      if (strDate[0] != '\0') {
        //-- Get nb visit from DB
        
        // Convert timestamp to Y-m-d
        ss.str(strDate);
        ss >> tStamp; //Ex: 1303639200;
        timeinfo = localtime(&tStamp);
        strftime(strDate, 31, "%Y-%m-%d", timeinfo);
        
        // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
        oss << strModule << '/' << strDate << '/' << i;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        if (visit.length() <= 0)
          visit = "0";
        // Return nb visit
        mg_printf(conn, "\"%d\":%s", i, visit.c_str());
        oss.str("");
      }
      if (i != max - 1) mg_printf(conn, "%s", ",");
    }
    mg_printf(conn, "%s", "}]");
    if (j != nbModules - 1) mg_printf(conn, "%s", ",");
  }
  
  //-- Set end JSON string in response.
  mg_printf(conn, "%s", "]");
  if (is_jsonp) {
    mg_printf(conn, "%s", ")");
  }
  
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
  char strModule[65];  // Modules name. Ex: gerer_connaissance
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
    if (strDate[0] != '\0')
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
  }
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%A %d %B", timeinfo);
  mg_printf(conn, "\"%d\":\"day\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Get the day from the first date received
  oss << "d_0";
  get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
  oss.str("");
  boost::gregorian::date d;
  if (strDate[0] != '\0') {
    // Convert timestamp to Y-m-d
    boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
    d = pt.date();
  
    //-- Build visits stats in response for each modules.
    sscanf(strModules, "%d", &nbApps);
    for(i = 0; i < nbApps; i++) {
      if (mode == "all") {
        oss << "p_" << i;
        get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
        oss.str("");
        if (strModule[0] == '\0') continue;
        mg_printf(conn, "[\"%s\",{", strModule); // Print application name
        if (c.DEBUG_REQUESTS) cout << "stats_app_day - app=" << strModule;

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

        //-- and each module in an app
        for(it=setModules.begin(), hourVisit = k = 0; it!=setModules.end(); ) {
          //-- Get nb visit from DB
          for(int l=0, max=DB_TIMES_SIZE;l<max;l++) {
            // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
            oss << *it << '/' << strType << "/" << boost::gregorian::to_iso_extended_string(d) << '/' << dbTimes[l];
            // Search Key (oss) in DB
            visit = dbw_get(db, oss.str());
            iVisit = 0;
            if (visit.length() > 0) {
              sscanf(visit.c_str(), "%d", &iVisit);
              ////if (*it == "bureau") cout << oss.str() << " = " << iVisit << endl;
            }
            oss.str("");
            int timeVal = 0;
            sscanf(dbTimes[l].c_str(), "%d", &timeVal);
            if (floor(timeVal/10) == k) {
              hourVisit += iVisit;
            } else {
              ////if (*it == "bureau") cout << "HERE k=" << k << " hourVisit=" << hourVisit << " dbTimes[i]=" << dbTimes[l] << endl;
              // Return nb visit
              mg_printf(conn, "\"%d\":%d, ", k, hourVisit);
              hourVisit = iVisit;
              k = floor(timeVal/10);
            }
          } 
          ////if (*it == "bureau") cout << "LAST k=" << k << " hourVisit=" << hourVisit << endl;
          // Return last nb visit
          mg_printf(conn, "\"23\":%d", hourVisit);
          oss.str("");
          
          if (c.DEBUG_REQUESTS) cout << *it << " => " << hourVisit << " visits." << endl;
          it++;
          if (it!=setModules.end()) mg_printf(conn, "%s", ",");
        }
      }
      else {
        oss << "m_" << i;
        get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
        oss.str("");
        if (strDate[0] != '\0')
          mg_printf(conn, "[\"%s\",{", strModule);
      
        //-- Get nb visit from DB
        k = hourVisit = 0;
        for(int l=0, max=DB_TIMES_SIZE;l<max;l++) {
          // Build Key ex: "creer_modifier_retrocession/2011-04-24/150";
          oss << strModule << '/' << boost::gregorian::to_iso_extended_string(d) << '/' << dbTimes[l];
          // Search Key (oss) in DB
          visit = dbw_get(db, oss.str());
          iVisit = 0;
          if (visit.length() > 0) {
            sscanf(visit.c_str(), "%d", &iVisit);
            ////cout << oss.str() << " = " << iVisit << endl;
          }
          oss.str("");
          int timeVal = 0;
          sscanf(dbTimes[l].c_str(), "%d", &timeVal);
          if (floor(timeVal/10) == k) {
            hourVisit += iVisit;
            ////cout << "hourVisit=" << hourVisit << endl;
          } else {
            ////cout << "HERE k=" << k << " hourVisit=" << hourVisit << endl;
            // Return nb visit
            mg_printf(conn, "\"%d\":%d, ", k, hourVisit);
            hourVisit = iVisit;
            k = floor(timeVal/10);
          }
        } 
        ////cout << "LAST k=" << k << " hourVisit=" << hourVisit << endl;
        // Return last nb visit
        mg_printf(conn, "\"23\":%d", hourVisit);
      }
    
      mg_printf(conn, "%s", "}]");
      if (i != nbApps - 1) mg_printf(conn, "%s", ",");
    }
  }
  
  //-- In all mode, add an "Others" application
  if (mode == "all" && setOtherModules.size() > 0) {
    mg_printf(conn, ", [\"%s\",{", "Autres"); // Print "Others" name
    
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    hourVisit = 0;
    //-- Get nb visit from DB
    for(int l = k = 0, max=DB_TIMES_SIZE;l<max;l++) {
      int timeVal = 0;
      sscanf(dbTimes[l].c_str(), "%d", &timeVal);
      for(it=setOtherModules.begin(), nbVisitForApp = 0; it!=setOtherModules.end(); it++) {
        if (c.DEBUG_APP_OTHERS && l == 0) cout << *it << ", ";
        // Build Key ex: "creer_modifier_retrocession/1/2011-04-24/150";
        oss << *it << '/' << strType << "/" << boost::gregorian::to_iso_extended_string(d) << '/' << timeVal;
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
        mg_printf(conn, "\"%d\":%d, ", k, hourVisit);
        hourVisit = nbVisitForApp;
        k = floor(timeVal/10);
      }
    }
    // Return last nb visit
    mg_printf(conn, "\"23\":%d", hourVisit);
    oss.str("");
    
    mg_printf(conn, "%s", "}]");
  }
  
  //-- Set end JSON string in response.
  mg_printf(conn, "%s", "]");
  if (is_jsonp) {
    mg_printf(conn, "%s", ")");
  }
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
  int i, j, max, nbModules;
  char strDates[11];   // Number of dates. Ex: 31
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strOffset[11];  // Date offset. Ex: 11
  char strModules[11]; // Number of modules. Ex: 4
  char strModule[65];  // Modules name. Ex: gerer_connaissance
  char strMode[4];     // Mode. Ex: app or all
  char strType[2];     // Mode. Ex: 1:visits, 2:views, 3:statics
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
  time_t tStamp;
  struct tm * timeinfo;

  //-- Get parameters in request.
  get_qsvar(ri, "mode", strMode, sizeof(strMode));
  assert(strMode[0] != '\0');
  mode = string(strMode);
  if (mode == "all") {
    get_qsvar(ri, "apps", strModules, sizeof(strModules));
  } else {
    get_qsvar(ri, "modules", strModules, sizeof(strModules));
  }
  get_qsvar(ri, "dates", strDates, sizeof(strDates));
  assert(strDates[0] != '\0');
  get_qsvar(ri, "offset", strOffset, sizeof(strOffset));
  assert(strOffset[0] != '\0');
  get_qsvar(ri, "type", strType, sizeof(strType));
  assert(strType[0] != '\0');
  
  //-- Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  //-- Set each date to according offset in response.
  sscanf(strOffset, "%d", &i);
  sscanf(strDates, "%d", &max);
  for(max += i; i < max; i++) {
    oss << "d_" << i;
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0')
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
  }
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%B %Y", timeinfo); // Depend one request intra, day, week, month, year
  mg_printf(conn, "\"%d\":\"month\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Build visits stats in response for each modules.
  sscanf(strModules, "%d", &nbModules);
  for(j = 0; j < nbModules; j++) {
    oss << "m_" << j;
    get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
    oss.str("");
    if (strDate[0] != '\0')
      mg_printf(conn, "[\"%s\",{", strModule);
      
    if (c.DEBUG_REQUESTS) cout << "stats_app_week: " << strModule << endl;
    
    //-- and each dates.
    sscanf(strOffset, "%d", &i);
    sscanf(strDates, "%d", &max);
    for(max += i; i < max; i++) {
      oss << "d_" << i;
      get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
      oss.str("");
      if (strDate[0] != '\0') {
        //-- Get nb visit from DB
        
        // Convert timestamp to Y-m-d
        boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
        boost::gregorian::date d = pt.date();
        
        // Build Key ex: "creer_modifier_retrocession/2011-04-24";
        oss << strModule << '/' << strType << "/" << boost::gregorian::to_iso_extended_string(d);
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        if (visit.length() <= 0)
          visit = "0";
        // Return nb visit
        mg_printf(conn, "\"%d\":%s", i, visit.c_str());
      }
      if (i != max - 1) mg_printf(conn, "%s", ",");
    }
    mg_printf(conn, "%s", "}]");
    if (j != nbModules - 1) mg_printf(conn, "%s", ",");
  }
  
  //-- Set end JSON string in response.
  mg_printf(conn, "%s", "]");
  if (is_jsonp) {
    mg_printf(conn, "%s", ")");
  }
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
  int i, j, max, nbApps, nbModules;
  char strDates[11];   // Number of dates. Ex: 31
  char strDate[31];    // Start date. Ex: 1314253853 or Thursday 25 November
  char strOffset[11];  // Date offset. Ex: 11
  char strModules[11]; // Number of modules. Ex: 4
  char strModule[65];  // Modules name. Ex: gerer_connaissance
  char strMode[4];     // Mode. Ex: app or all
  char strType[2];     // Mode. Ex: 1 or 2 or 3
  ostringstream oss;
  istringstream ss;
  string mode;
  string visit;
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
  get_qsvar(ri, "offset", strOffset, sizeof(strOffset));
  assert(strOffset[0] != '\0');
  get_qsvar(ri, "type", strType, sizeof(strType));
  assert(strType[0] != '\0');
  
  //-- Set begining JSON string in response.
  mg_printf(conn, "%s", standard_json_reply);
  is_jsonp = handle_jsonp(conn, ri);
  mg_printf(conn, "%s", "[{");
  
  if (c.DEBUG_REQUESTS) cout << "nb=" << strModules << endl;
  
  //-- Create a set for the Dates to loop easily
  sscanf(strOffset, "%d", &i);
  sscanf(strDates, "%d", &max);
  for(max += i; i < max; i++) {
    oss << "d_" << i;
    get_qsvar(ri, oss.str().c_str(), strDate, sizeof(strDate));
    oss.str("");
    if (strDate[0] != '\0') {
      // Convert timestamp to Y-m-d
      boost::posix_time::ptime pt = boost::posix_time::from_time_t(boost::lexical_cast<time_t> (strDate));
      boost::gregorian::date d = pt.date();
      setDate.insert(boost::gregorian::to_iso_extended_string(d));
      //-- Set each date to according offset in response.
      mg_printf(conn, "\"%d\":\"%s\",", i, strDate);
    }
  }
  
  //-- Set Mode and Date in response.
  // Extract "Day NDay Month" from timestamp
  ss.str(strDate);
  ss >> tStamp; //Ex: 1303639200;
  timeinfo = localtime(&tStamp);
  strftime(strDate, 31, "%B %Y", timeinfo); // Depend one request intra, day, week, month, year
  mg_printf(conn, "\"%d\":\"month\",\"%d\":\"%s\"},", i, i+1, strDate);
  
  //-- Build visits stats in response for each modules or app.
  sscanf(strModules, "%d", &nbApps);
  for(i = 0; i < nbApps; i++) {
    if (mode == "all") {
      oss << "p_" << i;
      get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
      oss.str("");
      if (strModule[0] == '\0') continue;
      mg_printf(conn, "[\"%s\",{", strModule); // Print application name
      if (c.DEBUG_REQUESTS) cout << "stats_app_month - app=" << strModule;
      
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
        //-- and each module in an app
        for(itt=setModules.begin(), nbVisitForApp = 0; itt!=setModules.end(); itt++) {
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
        // Return nb visit
        mg_printf(conn, "\"%d\":%d", j, nbVisitForApp);

        if (c.DEBUG_REQUESTS) cout << *it << " => " << nbVisitForApp << " visits." << endl;
        
        it++;
        if (it!=setDate.end()) mg_printf(conn, "%s", ",");
      }
    }
    else {
      oss << "m_" << i;
      get_qsvar(ri, oss.str().c_str(), strModule, sizeof(strModule));
      oss.str("");
      if (strModule[0] == '\0') continue;
      mg_printf(conn, "[\"%s\",{", strModule); // Print web module name

      if (c.DEBUG_REQUESTS) cout << "stats_app_month - module=" << strModule << endl;
      
      //-- and each dates.
      sscanf(strOffset, "%d", &j);
      for(it=setDate.begin(); it!=setDate.end(); j++) {
        //-- Get nb visit from DB
        // Build Key ex: "creer_modifier_retrocession/1/2011-04-24";
        oss << strModule << '/' << strType << '/' << *it;
        // Search Key (oss) in DB
        visit = dbw_get(db, oss.str());
        oss.str("");
        if (visit.length() <= 0)
        visit = "0";
        // Return nb visit
        mg_printf(conn, "\"%d\":%s", j, visit.c_str());
        
        it++;
        if (it!=setDate.end()) mg_printf(conn, "%s", ",");
      }
    }
    mg_printf(conn, "%s", "}]");
    if (i != nbApps - 1) mg_printf(conn, "%s", ",");
  }
  
  //-- In all mode, add an "Others" application
  if (mode == "all" && setOtherModules.size() > 0) {
    mg_printf(conn, ", [\"%s\",{", "Autres"); // Print "Others" name
    
    if (c.DEBUG_APP_OTHERS) cout << "Others modules: " << flush;
    
    sscanf(strOffset, "%d", &j);
    for(it=setDate.begin(); it!=setDate.end(); j++) {
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
      // Return nb visit
      mg_printf(conn, "\"%d\":%d", j, nbVisitForApp);
      
      it++;
      if (it!=setDate.end()) mg_printf(conn, "%s", ",");
    }
    mg_printf(conn, "%s", "}]");
    
    if (c.DEBUG_APP_OTHERS) cout << endl;
  }
  
  //-- Set end JSON string in response.
  mg_printf(conn, "%s", "]");
  if (is_jsonp) {
    mg_printf(conn, "%s", ")");
  }
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
  uint64_t i, max;
  int dayVisit, monthNumber;
  ostringstream oss;
  string visit;
  string strOss;
  set<string> setModules;
  set<string>::iterator it;
  
  // At the first start do a compression from the first day of the year
  boost::gregorian::date now(boost::gregorian::day_clock::universal_day());
  boost::gregorian::date last(now.year()-1, boost::gregorian::Jan, 1);
  
  // Hold the delay for non compressed stats
  boost::gregorian::date_duration dd_week(c.DAYS_FOR_DETAILS);
  
  /// FOR DEBUG purpose USE minutes + 1
  //boost::posix_time::ptime t = boost::posix_time::second_clock::universal_time() + boost::posix_time::minutes(LOGS_COMPRESSION_INTERVAL);
  /// FOR REAL usage USE a specific date/time : the next day at 3 o'clock
  boost::gregorian::date_duration dd(1);
  boost::posix_time::ptime t(boost::gregorian::day_clock::universal_day()/* + dd*/, boost::posix_time::time_duration(3,0,0));
  
  try {
    while(true)
    {
      boost::gregorian::date dateToHold(boost::gregorian::day_clock::universal_day() - dd_week);
      boost::posix_time::ptime timeNow (boost::posix_time::second_clock::universal_time());
      //cout << "Obj:" << boost::posix_time::to_simple_string(t) << " & now:" << boost::posix_time::to_simple_string(timeNow) << endl;
      if (timeNow >= t) {
        /// FOR DEBUG
        //t += boost::posix_time::minutes(2*c.LOGS_COMPRESSION_INTERVAL);
        /// FOR Real use +1 day
        t += boost::posix_time::hours(24);
        
        // Compression to file atomically
        boost::mutex::scoped_lock lock(mutex);
        cout << "----- COMPRESSION RUNNING now -----" << endl;
        
        // Reconstruct list of modules
        setModules.clear();
        getDBModules(setModules);
        
        //-- Reloop thru all days to j-x in order to remove details and store days only
        // Loop thru day since last parsing
        boost::gregorian::day_iterator ditr(last);
        for (;ditr <= dateToHold; ++ditr) {
          //produces "C: 2011-Nov-04", "C: 2011-Nov-05", ...
          cout << "C: " << to_simple_string(*ditr) << endl;
          monthNumber = ditr->month();
        
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
                  // Return nb visit
                  dayVisit += boost::lexical_cast<int>(visit);
                  if(c.DEBUG_LOGS && lineType == 1) cout << "C Found: " << strOss << '/' << dbTimes[i] << " = " << dayVisit << endl;
                  // Delete the current Key in DB
                  dbw_remove(db, strOss+'/'+dbTimes[i]);
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
        /*if(c.DEBUG_LOGS) cout << "C. Dump DB" << endl;
        vector< pair<string, string> > myDb = dbw_get_all(db);
      
        // Drop files
        if(c.DEBUG_LOGS) cout << "C. Drop DB files" << endl;
        dbw_drop(c.DB_PATH.c_str());
        //dbw_drop(db);  //-1.8.1
      
        // Restore DB
        if(c.DEBUG_LOGS) cout << "C. Restore DB" << endl;
        db = dbw_open(c.DB_BUFFER, c.DB_PATH.c_str());
        for (i=0, max=myDb.size(); i<max; i++) {
          dbw_add(db, myDb[i].first, myDb[i].second);
          if(c.DEBUG_LOGS) cout << i << "/" << max << " - key=" << myDb[i].first << " - value=" << myDb[i].second << endl;
        }*/
        dbw_flush(db);
        
        cout << "----- COMPRESSION END now -----" << endl;
      
        last = dateToHold;
        // Here is released the scoped mutex automatically
      }
      /// FOR DEBUG purpose USE 10 seconds
      ///boost::this_thread::sleep(boost::posix_time::seconds(10));
      ///FOR REAL USE 10 minutes
      boost::this_thread::sleep(boost::posix_time::minutes(10));
    }
  
  } catch(boost::interprocess::interprocess_exception &ex) {
    cerr << ex.what() << std::endl;
  }
  return;
}

/*!
 * \fn void readLogThread(const Config c)
 * \brief Do a continuous read of a file and call the line analyser.
 *
 * \param c Config file containing the path/name of file to read.
 */
void readLogThread(const Config c) {
  string data;
  unsigned long readPos = 0;
  
  //TODO:
  // Find last line of log in DB
  // Start from the corresponding date line in file
  
  try {
    while(true) {
      boost::this_thread::sleep(boost::posix_time::seconds(c.LOGS_READ_INTERVAL)); // interruptible
      
      // Write to file atomically
      boost::mutex::scoped_lock lock(mutex);
      
      time_t now = time(0);
      cout << "----- READ LOG now: " << now << " -----" << endl;
      
      //-- Reconstruct list of modules
      set<string> setModules;
      set<string>::iterator it;
      getDBModules(setModules);
    
      readPos = readLogFile(c, c.LOG_FILE_PATH, setModules, readPos);
      if (c.DEBUG_LOGS) cout << "Current pos in myfile.txt: " << readPos << "bytes." << endl;
    
      // Update list of modules in DB
      string modules = "";
      for(it=setModules.begin(); it!=setModules.end(); it++) {
        modules += *it + "/";
      }
      dbw_remove(db, "modules");
      dbw_add(db, "modules", modules);
      
      // Here is released the scoped mutex automatically
    }
  } catch(boost::interprocess::interprocess_exception &ex) {
    cerr << ex.what() << std::endl;
  }
  boost::interprocess::named_mutex::remove("fstream_named_mutex");
}

int main(void) {
  // Announce yourself
  cout << "Welcome to mooWApp." << endl;
  
  //-- Read configuration file
  
  //-- Open the database
  //db = dbw_open(c.DB_BUFFER, c.DB_PATH.c_str());
  Db db_(NULL, 0);
  db = dbw_open(&db_, c.DB_PATH.c_str());
  
  //-- DB Compact task set-up
  if (c.COMPRESSION) {
    cout << "DB task start..." << endl;
    boost::thread cThread(&compressionThread, c);
  }

  //-- Start reading file
  cout << "Read file task start..." << endl;
  boost::thread rThread(&readLogThread, c);
  
  //-- Json web server set-up
  struct mg_context *ctx;
  const char *soptions[] = {"listening_ports", c.LISTENING_PORT.c_str(), NULL};
  cout << "Server now listening on " << c.LISTENING_PORT << endl;
  ctx = mg_start(&callback, NULL, soptions);
  getchar();  // Wait until user hits "enter" or any car
  mg_stop(ctx);
  
  //-- DB Release
  dbw_close(db);
  
  return 0;
}
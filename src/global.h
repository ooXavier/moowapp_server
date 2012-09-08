/*!
 * \file global.h
 * \brief global static datas for mooWApp
 * \author Xavier ETCHEBER
 */

#ifndef MOOWAPP_STATS_GLOBAL_H_
#define MOOWAPP_STATS_GLOBAL_H_

static const std::string format("%d/%b/%Y");

static const char *standard_json_reply = "HTTP/1.1 200 OK\r\n"
  "Content-Type: application/x-javascript; charset=UTF-8\r\n"
  "Cache: no-cache\r\n"
  "Access-Control-Allow-Origin: *\r\n"
  "X-Powered-By: IHM-Stat-Server\r\n"
  "Connection: close\r\n\r\n";

/*!
 * \def DB_TIMES_SIZE 144
 * \brief Size of dbTimes.
 */
#define DB_TIMES_SIZE 144

static const std::string dbTimes [DB_TIMES_SIZE] = { "0","1","2","3","4","5","10","11","12","13","14","15",
  "20","21","22","23","24","25","30","31","32","33","34","35","40","41","42","43","44","45",
  "50","51","52","53","54","55","60","61","62","63","64","65","70","71","72","73","74","75",
  "80","81","82","83","84","85","90","91","92","93","94","95","100","101","102","103","104","105",
  "110","111","112","113","114","115","120","121","122","123","124","125","130","131","132","133","134","135",
  "140","141","142","143","144","145","150","151","152","153","154","155","160","161","162","163","164","165",
  "170","171","172","173","174","175","180","181","182","183","184","185","190","191","192","193","194","195",
  "200","201","202","203","204","205","210","211","212","213","214","215","220","221","222","223","224","225",
  "230","231","232","233","234","235"};

static const std::string KEY_MODULES("modules");
static const std::string KEY_DELETED_MODULES("modules-deleted");

#endif // MOOWAPP_STATS_GLOBAL_H_
/*!
 * \file log_reader.h
 * \brief Functions to read web logs files for mooWApp
 * \author Xavier ETCHEBER
 */

#ifndef MOOWAPP_STATS_LOG_READER_H_
#define MOOWAPP_STATS_LOG_READER_H_

#include <string>
#include <set> // Set of modules

// database
#include <db_cxx.h>

extern Db *db;

/*!
 * \struct SslLog
 * \brief Structure qui représente une ligne de access log.
 */
struct SslLog {
  std::string app;
  std::string group;
  std::string type;
  std::string id_url;
  std::string date_d;
  std::string date_t_minutes;
  std::string date_t;
  std::string date_t_hours;
  std::string logKey;
  std::string responseSize;
  std::string responseDuration;
  int visit;
};

/*!
 * \fn string findExtInLine(map<string, set<string> > &mapExtensions, const string &line)
 * \brief Find an extension configured in a line. Return group name if found.
 *
 * \param[in, out] mapExtensions Map of extensions (from config object).
 * \param[in] line to be analysed.
 */
std::string findExtInLine(std::map<std::string, std::set<std::string> > &mapExtensions, const std::string &line);

/*!
 * \fn void insertLogLine(SslLog &logLine, set<string> &setModules)
 * \brief Insert a new row of visit in stat DB (or do a +1 on an existing line).
 *
 * \param[in] strLog String format of log line to insert in DB.
 */
bool insertLogLine(const std::string &strLog);

/*!
 * \fn void analyseLine(string line, set<string> &setModules)
 * \brief Filter the usefull stats from a string that represent a line of log
 *
 * \param logFileNb Log file number in configuration (for debugging purpose).
 * \param line The log line to be parsed.
 * \param setModules The set of web modules already known.
 */
bool analyseLine(const unsigned short &logFileNb, const std::string &line, std::set<std::string> &setModules);

/*!
 * \fn unsigned long readLogFile(const unsigned short &logFileNb, const std::string &strFile, std::set<std::string> &setModules, uint64_t readPos = 0)
 * \brief Read a file and call the line analyser for each line
 *
 * \param logFileNb Log file number in configuration (for debugging purpose).
 * \param strFile The file to be used as log file.
 * \param setModules The set of web modules already known.
 * \param readPos The position in the log file strFile.
 */
uint64_t readLogFile(const unsigned short &logFileNb, const std::string &strFile, std::set<std::string> &setModules, uint64_t readPos = 0);

#endif // MOOWAPP_STATS_LOG_READER_H_
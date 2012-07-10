/*!
 * \file configuration.h
 * \brief Configuration reader for mooWApp
 * \author Xavier ETCHEBER
 */

#ifndef MOOWAPP_STATS_CONFIGURATION_H_
#define MOOWAPP_STATS_CONFIGURATION_H_

#include <string>

class Config
{
public:
  bool DEBUG_LOGS; // Log mode?
  bool DEBUG_REQUESTS; // Log incoming requests
  bool DEBUG_APP_OTHERS; // Log apps
  
  std::string DB_PATH; // PATH to db's folder
  int DB_BUFFER; // maximum size of db to store in memory
  
  std::string FILTER_PATH; // %PATH% of the log files to analyse for insertion
  std::string FILTER_SSL; // %NAME% of the log files to analyse for insertion
  
  std::string FILTER_EXTENSION; // Extension to search in (ssl_)access_log files
  std::string FILTER_URL1; // First string to search in (ssl_)access_log files
  std::string FILTER_URL2; // Second string to search in (ssl_)access_log files
  std::string FILTER_URL3; // Third string to search in (ssl_)access_log files
  std::string EXCLUDE_MOD; // Substring of module to exclude from stats
  
  bool COMPRESSION;
  int LOGS_READ_INTERVAL; // in seconds
  static const int LOGS_COMPRESSION_INTERVAL = 5; // in minutes
  static const int DAYS_FOR_DETAILS = 7; // Days of non compressed stats stored
  std::string LISTENING_PORT; // Server listening port
  std::string LOG_FILE_FORMAT; // Format of the log file
  std::string LOG_FILE_PATH;

  /*!
   * \fn void readConfig(string cfgFile = "ConfigurationN.ini")
   * \brief Read a file and set-up configuration values
   *
   * \param cfgFile The file to be used as configuration file.
   */
  Config(std::string cfgFile = "configuration.ini");

private:

/*!
 * \fn void trimInfo(string& s)
 * \brief Remove leading and trailing whitespace
 *
 * \param s The string to clean from whitespaces.
 */
void trimInfo(std::string& s);

};

#endif // MOOWAPP_STATS_CONFIGURATION_H_
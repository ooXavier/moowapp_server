/*!
 * \file global.cpp
 * \brief Library o functions to use.
 * \author Xavier ETCHEBER
 */

#include <string>
#include <boost/spirit/include/karma.hpp>

// mooWApp
#include "global.h"

/*!
 * \fn int getMonth(const string &month)
 * \brief Return month number from short string representation.
 *
 * \param[in] month as 3 chars.
 */
int getMonth(const std::string &month) {
  for(unsigned short i = 0; i<12; i++)
    if(month == MONTHS[i]) return i+1;
  return 0;
}

unsigned int stringToInt(const std::string &str) {
  const char *p = str.c_str();
  int x = 0;
  bool neg = false;
  if (*p == '-') {
    neg = true;
    ++p;
  }
  while (*p >= '0' && *p <= '9') {
    x = (x*10) + (*p - '0');
    ++p;
  }
  if (neg) {
    x = -x;
  }
  return x;
}

bool intToString(std::string &str, const unsigned int val) {
  namespace karma = boost::spirit::karma;
  std::back_insert_iterator<std::string> sink(str);
  return karma::generate(sink, karma::int_, val);
}

/*!
 * \fn void printProgBar(int percent)
 * \brief Display a progress bar
 *
 * \param[in] percent.
 */
void printProgBar(int percent) {
  std::string bar;
  for(int i=0; i < 50; i++){
    if( i < (percent/2)){
      bar.replace(i,1,"=");
    }else if( i == (percent/2)){
      bar.replace(i,1,">");
    }else{
      bar.replace(i,1," ");
    }
  }

  std::cout<< "\r" "[" << bar << "] ";
  std::cout.width(3);
  std::cout<< percent << "%     " << std::flush;
}
#!/bin/sh

APP="mooWApp Server"
PID_FILE="mwa.pid"
CONF_FILE="configuration.ini"
APP_BIN=moowapp_server

function do_start {
  nohup ./$APP_BIN >mwa.log 2>&1 &
  echo $! > $PID_FILE
}

function do_stop {
  NB_PROCESS=`ps ax | grep $APP_BIN | grep -v grep | wc -l`
  if [ $NB_PROCESS -gt 1 ]; then
    echo "ERROR: multiple $APP processes found, you'd better kill thoses processes by hand with kill -2."
  elif [ $NB_PROCESS -eq 1 ]; then
    if [ -f $PID_FILE ]; then
      PID=$(cat $PID_FILE)
      NB_PROCESS=`ps hax $PID | grep $APP_BIN | grep -v grep | wc -l`
      if [ $NB_PROCESS -eq 1 ]; then
        kill -2 $PID
      else
        echo "ERROR: process N° $PID does not seem to be $APP"
        echo "kill $APP by hand with kill -2"
      fi
    fi
  else
    echo "WARNING: are you sure $APP is running ?"
  fi
}

if [ ! -f "$CONF_FILE" ]; then
  echo "ERROR: file not found : '$CONF_FILE'"
  exit 1
fi

case "$1" in
  start)
echo "Starting $APP"
NB_PROCESS=`ps ax | grep $APP_BIN | grep -v grep | wc -l`
if [ $NB_PROCESS -eq 0 ]; then
do_start
else
echo "ERROR: $APP is already running"
fi
;;
  stop)
echo "Stopping $APP"
do_stop
;;

status)
NB_PROCESS=`ps ax | grep $APP_BIN | grep -v grep | wc -l`
if [ $NB_PROCESS -gt 1 ]; then
echo "WARNING: multiple $APP processes found !"
elif [ $NB_PROCESS -eq 1 ]; then
echo "running :)"
else
echo "not running :("
fi
;;

  *)
PROG_NAME=`basename $0`
echo "Usage: $APP_BIN {start|stop|status}"
exit 1
esac

exit 0

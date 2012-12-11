# INSTALL PATHS
BOOST = /usr
DATABASE = /usr
MONGOOSE = mongoose

# COMPILATION SETTINGS
CC = g++
DEBUG = -g -DDEBUG_LOGS -DDEBUG_REQ
CFLAGS = -c -Wall -I$(BOOST)/include -I$(DATABASE)/include -I$(MONGOOSE) -pthread $(DEBUG)
# If you want to encapsule all into one file, on unix add -Wl,-rpath,/usr/local/lib:/usr/lib at the end of LDFLAGS
# and make sure you have libstdc++.a into one of those two folders
LDFLAGS = -L$(DATABASE)/lib -L$(MONGOOSE) -L$(BOOST)/lib
LIBS = -ldb_cxx -lboost_thread-mt -lboost_date_time-mt -lboost_system-mt -ldl
SOURCES = src/configuration.cpp src/log_reader.cpp src/db_access_berkeleydb.cpp src/moowapp_server.cpp mongoose/mongoose.c
OBJECTS = $(SOURCES:.cpp=.o)
EXECUTABLE = bin/moowapp_server

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f src/configuration.o src/log_reader.o src/db_access_berkeleydb.o src/moowapp_server.o
	-rm -f $(EXECUTABLE)

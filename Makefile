CC=g++
BOOST=/opt/local
DATABASE=/Users/Xavier/DEVz/C++/BerkeleyDB.5.3
MONGOOSE=mongoose
CFLAGS=-c -Wall -I$(BOOST)/include -I$(DATABASE)/include -I$(MONGOOSE) -pthread -g
# If you want to encapsule all into one file, on unix add -Wl,-rpath,/usr/local/lib:/usr/lib at the end of LDFLAGS
# and make sure you have libstdc++.a into one of those two folders
LDFLAGS=-L$(DATABASE)/lib -L$(MONGOOSE) -L$(BOOST)/lib
LIBS= -ldb_cxx-5.3 -lboost_thread-mt -lboost_date_time -lboost_system
SOURCES=src/configuration.cpp src/log_reader.cpp src/db_access_berkeleydb.cpp src/moowapp_server.cpp mongoose/mongoose.c
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=bin/moowapp_server

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f src/configuration.o src/log_reader.o src/db_access_berkeleydb.o src/moowapp_server.o
	-rm -f $(EXECUTABLE)
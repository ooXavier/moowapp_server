CC=g++
BOOST=/opt/local
DATABASE=/Users/Xavier/DEVz/C++/BerkeleyDB.5.3
CFLAGS=-c -O2 -Wall -I$(BOOST)/include -I$(DATABASE)/include
LDFLAGS=-L$(DATABASE)/lib -L$(BOOST)/lib
LIBS=-lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt -lboost_program_options-mt -lboost_iostreams-mt -lboost_date_time-mt -lboost_regex-mt -ldb_cxx-5.3 -ldl
SOURCES=../../src/configuration.cpp ../../src/log_reader.cpp ../../src/db_access_berkeleydb.cpp upgrade_to_0.2.4.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=bin/moowapp_upgrade_to_0.2.4

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS) $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f src/configuration.o src/log_reader.o src/db_access_berkeleydb.o src/moowapp_insert.o
	-rm -f $(EXECUTABLE)

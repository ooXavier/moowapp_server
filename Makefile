CC=g++
BOOST=/opt/local
DATABASE=/Users/Xavier/DEVz/C++/BerkeleyDB.5.3
MONGOOSE=mongoose
CFLAGS=-c -Wall -I$(BOOST)/include -I$(DATABASE)/include -I$(MONGOOSE) -pthread -g
# If you want to encapsule all into one file, on unix add -Wl,-rpath,/usr/local/lib:/usr/lib at the end of LDFLAGS
# and make sure you have libstdc++.a into one of those two folders
LDFLAGS=-L$(DATABASE)/lib -L$(MONGOOSE) -L$(BOOST)/lib
LIBS= -ldb_cxx-5.3 -lboost_thread-mt -lboost_date_time -lboost_system
SOURCES=configuration.cpp log_reader.cpp db_access_berkeleydb.cpp moowapp_server.cpp mongoose/mongoose.c
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=moowapp_server

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f configuration.o log_reader.o db_access_berkeleydb.o moowapp_server.o
	-rm -f $(EXECUTABLE)
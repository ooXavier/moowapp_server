Web Statistics DB Server aka : mooWApp       {#index}
======================================
By 2011-2012 ooXavier

<pre>
                       __        __ _                  
 _ __ ___    ___    ___\ \      / // \    _ __   _ __  
| '_ ` _ \  / _ \  / _ \\\ \ /\ / // _ \  | '_ \ | '_ \ 
| | | | | || (_) || (_) |\ V  V // ___ \ | |_) || |_) |
|_| |_| |_| \\___/  \\___/  \\_/\\_//_/   \\_\\| .__/ | .__/ 
                                         |_|    |_|    
</pre>

[![Build Status](https://secure.travis-ci.org/ooXavier/moowapp_server.png)](http://travis-ci.org/ooXavier/moowapp_server)

## Typical usage
Count visits for certains URLs each day and let the stats be accessible thru an Intranet / Internet.

##  Advantages
- No modification of apps (no hidden image, no JavaScript needed)
- Administrators can monitor specific pages (filtering log content)
- RoundRobin mecanism to keep a small DB.

## Installation
1. Download and install Boost
2. Download and install BerkeleyDB

	<pre>
	Go to http://www.oracle.com/technetwork/database/berkeleydb/downloads/index-082944.html
	Download this one "Berkeley DB 5.3.21.NC.tar.gz , without encryption" (34M)
	$ tar zxvf "Berkeley DB 5.3.15.NC.tar.gz"
	$ cd db-xxxxxxx/build_unix
	$ ../dist/configure --enable-cxx --enable-stl (if you want to change default install folder add --prefix="FOLDER TO INSTALL"-
	(default path are : /usr/local/BerkeleyDB.5.3 for app with docs and /usr/local/BerkeleyDB.5.3/lib for libs)
	$ make
	$ sudo make install
	</pre>

3. Build app with

    make && make -f MakefileInsert
4. Change the configuration.ini to set-up your folder to your web logs files
5. Then run...

    ./moowapp.sh start

## Installation

Start the app with

    ./moowapp.sh start
Check if the app is running with

    ./moowapp.sh status
Stop with

    ./moowapp.sh stop

## Insertion program usage

If you want to insert data from existing log files before, running the server you can launch

    bin/moowapp_insert

You will get for example :

	<pre>
  DB /data/berkeleydb/storage.db connected
  START with 0 modules.
  Reading ssl_access.log.2012-10-09
  [==================================================] 100%     
  Reading ssl_access.log.2012-10-10
  [==================================================] 100%     
  Reading ssl_access.log.2012-10-30
  [==================================================] 100%     
  NB Modules: 162
  Removed
  Re-added
  Closing db connection
  499.43 s</pre>

## Changelogs
### V0.1
- First alpha version running on BerkeleyDB and Mongoose

### V0.2
- Version currently running for 6 months now on a server AIX 5.3 TL12
- Add a startup script
- Refactor folders

### V0.2.2
- Manage request by POST & GET

### V0.2.3
- Add administrative functions : list webapps, merge webapps stats
- Fix #1, error on concurrent requests

### V0.2.4
- Add support other pages than visits (img, css, js and html/xml files) with the ability to group them in the configuration file
- Add a detailed view by 60 seconds

### V0.2.5
- Add the support for multiple log files : Change configuration to be able to read several logs files.

## Performance number
- V0.2.3 : Core 2 Duo 2,8Ghz can filter and add in DB up to 11 926 lines / second.
- V0.2.6 : Core i7 3,4Ghz can filter and add in DB up to 18 901 lines / second (with 5x more data stored).
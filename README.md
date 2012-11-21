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

## Manual
1. Change the configuration.ini to set-up your folder to your web logs files
2. Download and install BerkeleyDB (or any other db system: Kyoto Cabinet, Google LevelDB, nessDB ...)

    Go to http://www.oracle.com/technetwork/database/berkeleydb/downloads/index-082944.html
    Download this one "Berkeley DB 5.3.15.NC.tar.gz , without encryption" (34M)
    $ tar zxvf "Berkeley DB 5.3.15.NC.tar.gz"
    $ cd db-xxxxxxx
    $ cd build_unix (or the one adapted to your situation)
    $ ../dist/configure --prefix="FOLDER TO INSTALL"
    $ make
    $ make install
    (default path are : /usr/local/BerkeleyDB.5.3 for app and /usr/local/BerkeleyDB.5.3/lib for libs)
2. Build mongoose with

    cd mongoose && make (linux|bsd|solaris|mac|windows|mingw) && cd ..
3. Build app with

    make
4. Then run...

    ./moowapp.sh start

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

## Performance number
- V0.2.3 : Core 2 Duo 2,8Ghz can filter and add in DB up to 11 926 lines / second.
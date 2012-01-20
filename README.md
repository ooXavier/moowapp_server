                     __        ___                
 _ __ ___   ___   ___\ \      / / \   _ __  _ __  
| '_ ` _ \ / _ \ / _ \\ \ /\ / / _ \ | '_ \| '_ \ 
| | | | | | (_) | (_) |\ V  V / ___ \| |_) | |_) |
|_| |_| |_|\___/ \___/  \_/\_/_/   \_\ .__/| .__/ 
                                     |_|   |_|

# Web Statistics DB Server aka : mooWApp
By 2011-2012 ooXavier

## Typical usage
Count visits for certains URLs each day and let the stats be accessible thru an Intranet / Internet.

##  Advantages
- No modification of apps (no hidden image, no JavaScript needed)
- Administrators can monitor specific pages (filtering log content)
- RoundRobin mecanism to keep a small DB.

## Manual
1. Change the configuration.ini to set-up your folder to your web logs files
2. Build mongoose with

    cd mongoose && make (linux|mac|windows) && cd ..
3. Build app with

    make
4. Then run...

    ./moowapp_server

## Changelogs
### V0.1
- First alpha version running on BerkeleyDB and Mongoose



* * *
  BE CAREFULL NOT TO USE THIS APP FOR RIGHT NOW: DEVELOPPMENT IN PROGRESS	
* * *
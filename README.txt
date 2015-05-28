
This is the project 3 extra credit.

To compile my code, simple type: make
The folder needs to contain a locals.txt and
a folder cached_files containing the cached 
files.

I have updated my webserver to connect with
simplecached and to use curl. For each request,
the cache is queried first. If the file is
not found, then the curl is queried.

The updated server works fine with my own 
test, using both my webclient and the provided
gfclient_download and gfclient_measure.



spiffify
========

Stupid quick program to re-arrange any spotify playlist into a hierarchy of Artist->Album for browsing that way.

This currently does not sort, is destructive of the existing Spiffify master folder, and generally is a total hack fest.

Requires libspotify to be installed.  This also requires your libspotify dev key to be copied in the dir as "key.h"

I have not even made a Makefile for this because I am super lazy.

Compilation on Mac:

gcc main.c -o spiffify -L. -lspotify

If you have libspotift.framework in a non-standard location like I do:

install_name_tool -change @loader_path/../Frameworks/libspotify.framework/libspotify ~/Downloads/libspotify-12.1.51-Darwin-universal/libspotify.framework/libspotify spiffify

Run it like:

./spiffify -u riley@mosey.org -p xxxxxxxx -l <name of spotify source list to clone into new hierarchy>



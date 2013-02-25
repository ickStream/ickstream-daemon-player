# --------------------------------------------------------------
#
# Description     : makefile the ickstream player deamon
#
# Comments        : -
#
# Date            : 13.02.2013
#
# Updates         : -
#
# Author          : //maf
#                  
# Remarks         : -
#
# Copyright (c) 2013 ickStream GmbH.
# All rights reserved.
# ------------------------------------------------------------------------
# DO NOT EDIT (generated on So 24. Feb 19:57:04 CET 2013 by /home/maf/Projects/ickstream/ickpd/configure)

SHELL=/bin/sh

CC              = cc
CFLAGS          = -pipe -g -Wall 
LD              = $(CC)
LDFLAGS         = -g -rdynamic
MKDEPFLAGS      = -Y


# Where to finde the: ickstream p2p library
ICKSTREAMDIR    = /home/maf/Projects/ickstream/ickpd/../ickstream-p2p


# Name of executable
EXECUTABLE      = ickpd


# Source files to process
SRC             = config.c persist.c ickDevice.c ickMessage.c playlist.c audio.c player.c ickpd.c
OBJECTS         = $(SRC:.c=.o)


# Includes and libraris
INCLUDES        = -I$(ICKSTREAMDIR)/include
LIBDIRS         = -L$(ICKSTREAMDIR)/lib
LIBS            = -lickstreamp2p -ljansson -lpthread -lz 


# How to compile c source files
%.o: %.c Makefile
	$(CC) $(INCLUDES) $(CFLAGS) $(DEBUGFLAGS) -c $< -o $@

all: $(EXECUTABLE)

new: cleanall all

# Build target
$(EXECUTABLE): $(OBJECTS) Makefile
	@echo '*************************************************************'
	@echo "Linking executable:"
	$(LD) $(LDFLAGS) $(LIBDIRS) $(OBJECTS) $(LIBS)  -o $@


# How to create dependencies
depend:
	@echo '*************************************************************'
	@echo "Creating dependencies:"
	makedepend $(MKDEPFLAGS) -- $(INCLUDES) $(CFLAGS) -- $(SRC) 2>/dev/null


# How to clean tempoarary files
clean:
	@echo '*************************************************************'
	@echo "Deleting intermediate files:"
	rm -f $(OBJECTS)


# How to clean all
cleanall: clean
	@echo '*************************************************************'
	@echo "Clean all:"
	rm -rf $(EXECUTABLE)

# End of Makefile -- makedepend output might follow ...
# DO NOT DELETE

config.o: ickpd.h config.h
persist.o: ickpd.h persist.h
ickDevice.o: ickpd.h
ickDevice.o: /home/maf/Projects/ickstream/ickstream-p2p/include/ickDiscovery.h
ickDevice.o: ickDevice.h
ickMessage.o: /home/maf/Projects/ickstream/ickstream-p2p/include/ickDiscovery.h
ickMessage.o: ickpd.h ickMessage.h audio.h playlist.h player.h
playlist.o: /home/maf/Projects/ickstream/ickstream-p2p/include/ickDiscovery.h
playlist.o: ickpd.h persist.h playlist.h
audio.o: ickpd.h audio.h persist.h
player.o: ickpd.h persist.h player.h
ickpd.o: /home/maf/Projects/ickstream/ickstream-p2p/include/ickDiscovery.h
ickpd.o: ickpd.h config.h persist.h ickDevice.h ickMessage.h playlist.h
ickpd.o: audio.h player.h

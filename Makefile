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
# DO NOT EDIT (generated on Mo 25. Feb 22:36:27 CET 2013 by /home/maf/Projects/ickstream/ickpd/configure)

SHELL=/bin/sh

SUBDIRS = daemon 
ARCVER  = $(shell TZ=UTC date '+%y%m%d%H%M%S')
ARCNAME = ickpd


all: 
	@echo '*************************************************************'
	@echo ' Building $(ARCNAME) ...'  
	@for a in $(SUBDIRS); do \
          cd $$a; if ! $(MAKE); then exit 1; fi; \
          cd /home/maf/Projects/ickstream/ickpd; \
        done
	@echo '*************************************************************'
	@echo '        >>>>  $(ARCNAME) was build succsessfully  <<<<'
	@echo '*************************************************************'

new: cleanall all

config: scripts/config.status

scripts/config.status: configure
	@./configure

Makefile: Makefile.in scripts/config.status
	@echo '*************************************************************'
	@scripts/config.status Makefile.in

dist: cleanall
	@echo '*************************************************************'
	@echo 'Building archive: '
	@echo "#define ICKPD_DISTRIB $(ARCVER)"> include/distrib.h
	@touch THIS_IS_VERSION_$(ARCVER)
#	@chmod -R g+w .
	@tar cvzf $(ARCNAME)$(ARCVER).tgz * 
	@ln -sf $(ARCNAME)$(ARCVER).tgz $(ARCNAME).tgz
	@date >./lastdist

incr: cleanall
	@echo '*************************************************************'
	@echo 'Building incremental update: '
	touch configure
	tar cvzf $(ARCNAME)-incr-$(ARCVER).tgz `find . -type f -newer ./lastdist -print`
	date >./lastdist

cleanall: clean
	@echo '*************************************************************'
	@echo -n 'Clean all: '
	@find . -name "*.a"            -exec rm -f '{}' ';'
	@find . -name "*.o"            -exec rm -f '{}' ';'
	@find . -name "*~"             -exec rm -f '{}' ';'
	@find . -name "core"           -exec rm -f '{}' ';'
	@find . -name "#*#"            -exec rm -f '{}' ';'
	@find . -name "config.status"  -exec rm -f '{}' ';'
	@find . -name ".ickpd_persist" -exec rm -f '{}' ';'
	@rm -f *.tgz 
	@rm -f THIS_IS_VERSION*
	@for a in $(SUBDIRS); do \
          cd $$a; $(MAKE) cleanall; \
          rm -f Makelie; \
          cd /home/maf/Projects/ickstream/ickpd; \
        done
	@echo done.

clean: 
	@echo '*************************************************************'
	@echo -n 'Cleaning directories: '
	@for a in $(SUBDIRS); do \
	  cd $$a; $(MAKE) clean; \
	  cd /home/maf/Projects/ickstream/ickpd; \
	done
	@find . -name "*.bak" -exec rm '{}' ';'
	@echo done.

install: all
	@echo '*************************************************************'
	@echo "todo..."
	@echo " done."

depend: 
	@echo '*************************************************************'
	@echo "Building dependencies:"
	@for a in $(SUBDIRS); do \
	  cd $$a; $(MAKE) depend; \
	  cd /home/maf/Projects/ickstream/ickpd; \
	done

tags:
	@echo '*************************************************************'
	@echo -n "Building taglist : "
	@etags -d -t `find src -name '*.c' -print` include/*.h
	@echo done.

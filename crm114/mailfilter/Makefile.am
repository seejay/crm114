## Process this file with automake to produce Makefile.in

## This script is free software; you can distribute it and/or modify it
## under the terms of the GNU GPL.  See the file COPYING.

## Copyright (C) 2003, 2004 Joost van Baal
## Copyright (C) 2007-2009 Ger Hobbelt <ger@hobbelt.com>

PACKAGE = @PACKAGE_TARNAME@

SILENT = @SILENT@


cdir = $(libexecdir)/$(PACKAGE)

c_SCRIPTS = \
	classifymail.crm \
	mailfilter.cf \
	mailfilter.crm \
	maillib.crm \
	mkcss.crm \
	mailreaver.crm \
	mailtrainer.crm

EXTRA_DIST = $(c_SCRIPTS) \
	mailfilter.cf.in \
	priolist.mfp \
	rewrites.mfp \
	blacklist.mfp \
	whitelist.mfp \
	whitelist.mfp.example


DISTCLEANFILES = 
CLEANFILES = *~ *.bak




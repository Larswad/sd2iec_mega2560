# Hey Emacs, this is a -*- makefile -*-

ifndef CONFIG
  CONFIG = config
endif

CONFDATA     := $(shell scripts/configparser.pl --confdata $(CONFIG))
CONFIGSUFFIX := $(word 1,$(CONFDATA))
OBJDIR       := obj-$(CONFIGSUFFIX)
CONFFILES    := $(wordlist 2,99,$(CONFDATA))

ifndef CONFFILES
.PHONY: missing_conf
missing_conf:
	@echo "ERROR: Please use $(MAKE) CONFIG=... to specify at least one config file."
	@echo "   (if you did, at least one file seems to be missing)"
endif

export CONFIGSUFFIX CONFIG OBJDIR

# Enable verbose compilation with "make V=1"
ifdef V
 Q :=
 E := @:
else
 Q := @
 E := @echo
endif

all: $(OBJDIR) $(OBJDIR)/make.inc
	$(Q)$(MAKE) --no-print-directory -f scripts/Makefile.main

$(OBJDIR)/make.inc: $(CONFFILES) | $(OBJDIR)
	$(E) "  CONFIG $(CONFFILES)"
	$(Q)scripts/configparser.pl --genfiles --makeinc $(OBJDIR)/make.inc --header $(OBJDIR)/autoconf.h $(CONFIG)

$(OBJDIR):
	$(E) "  MKDIR  $(OBJDIR)"
	-$(Q)mkdir $(OBJDIR)

copy clean fuses program: FORCE | $(OBJDIR) $(OBJDIR)/make.inc
	$(Q)$(MAKE) --no-print-directory -f scripts/Makefile.main $@

FORCE: ;
